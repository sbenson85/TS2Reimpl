#include "gz/cGZDBPFIndexModifiable.h"

#include "game/vtable.h"

#include <algorithm>
#include <new>

namespace {

using Self = cGZDBPFIndexModifiable;
using Node = Self::Map::value_type;

// The key map's slots 3 (file form -> caller's) and 4 (back), the filter's slot 3.
using KeyMapFn = void(__thiscall *)(void *, cGZPersistResourceKey *, cGZPersistResourceKey *);
using FilterFn = bool(__thiscall *)(void *, const cGZPersistResourceKey *);

} // namespace

cGZDBPFIndexModifiable::cGZDBPFIndexModifiable() = default;

cGZDBPFIndexModifiable::~cGZDBPFIndexModifiable() = default;

bool cGZDBPFIndexModifiable::IsModifiable()
{
    return true;
}

bool cGZDBPFIndexModifiable::IsEmpty()
{
    return mMap.empty();
}

uint32_t cGZDBPFIndexModifiable::GetCount()
{
    return static_cast<uint32_t>(mMap.size());
}

uint32_t cGZDBPFIndexModifiable::GetTotalSize()
{
    uint32_t total = 0;
    for (const Node &n : mMap) total += n.second.size;
    return total;
}

uint32_t cGZDBPFIndexModifiable::GetTotalMemSize()
{
    uint32_t total = 0;
    for (const Node &n : mMap) total += n.second.memSize & ~kFlag;
    return total;
}

uint32_t cGZDBPFIndexModifiable::GetDataEnd()
{
    // The larger of the running maximum and each end (cmovae), starting at 0.
    uint32_t maxEnd = 0;
    for (const Node &n : mMap) {
        uint32_t end = n.second.offset + n.second.size;
        if (maxEnd < end) maxEnd = end;
    }
    return maxEnd;
}

uint32_t cGZDBPFIndexModifiable::GetSerializedSize()
{
    return (static_cast<uint32_t>(mMap.size()) << 5) + 0x24;
}

void cGZDBPFIndexModifiable::CollectTypes(std::set<uint32_t> *types)
{
    for (const Node &n : mMap) types->insert(n.first.type);
}

void cGZDBPFIndexModifiable::CollectGroups(std::set<uint32_t> *groups)
{
    for (const Node &n : mMap) groups->insert(n.first.group);
}

uint32_t cGZDBPFIndexModifiable::GetResourceKeyList(cGZDBPFResourceKeyList *list, void *filter)
{
    std::vector<const Node *> found;
    uint32_t count = 0;
    if (filter == nullptr) {
        const size_t records = mMap.size();
        if (records != 0) found.reserve(records);
    }
    for (const Node &n : mMap) {
        if (n.first.type == kDirectoryType) continue;
        if (filter != nullptr) {
            const cGZPersistResourceKey key = n.first;
            if (!VirtualCall<FilterFn, 3>(filter, &key)) continue;
        }
        if (list != nullptr) found.push_back(&n);
        ++count;
    }
    if (list != nullptr) {
        // In file order.
        std::sort(found.begin(), found.end(), [](const Node *a, const Node *b) { return a->second.offset < b->second.offset; });
        const size_t old = list->size();
        list->resize(old + found.size());
        cGZPersistResourceKey *out = list->data() + old;
        for (const Node *n : found) *out++ = n->first;
    }
    return count;
}

cGZDBPFIndexModifiable::Map::iterator cGZDBPFIndexModifiable::Find(const cGZPersistResourceKey *key, uint8_t anyResource,
                                                                   uint8_t resourceZero)
{
    auto it = mMap.find(*key);
    if (it != mMap.end()) return it;
    const bool any = anyResource != 0 && key->resource == 0;
    const bool zero = resourceZero != 0 && key->resource != 0;
    if (!any && !zero) return mMap.end();
    for (it = mMap.begin(); it != mMap.end(); ++it) {
        const Key &k = it->first;
        if (k.instance == key->instance && k.type == key->type && k.group == key->group && (any || k.resource == 0))
            return it;
    }
    return it;
}

cGZDBPFIndexRecord *cGZDBPFIndexModifiable::FindRecord(const cGZPersistResourceKey *key, uint32_t anyResource,
                                                       uint32_t resourceZero)
{
    auto it = Find(key, static_cast<uint8_t>(anyResource), static_cast<uint8_t>(resourceZero));
    return it == mMap.end() ? nullptr : &it->second;
}

cGZDBPFIndexRecord *cGZDBPFIndexModifiable::FindRecordForWrite(const cGZPersistResourceKey *key, uint32_t anyResource,
                                                               uint32_t resourceZero)
{
    // The same function as slot 11 in the original's vtable.
    auto it = Find(key, static_cast<uint8_t>(anyResource), static_cast<uint8_t>(resourceZero));
    return it == mMap.end() ? nullptr : &it->second;
}

void cGZDBPFIndexModifiable::Add(const cGZPersistResourceKey &key, const cGZDBPFIndexRecord &record)
{
    mMap.try_emplace(key, record);
}

void cGZDBPFIndexModifiable::AddRecord(const cGZPersistResourceKey *key, const cGZDBPFIndexRecord *record)
{
    Add(*key, *record);
}

bool cGZDBPFIndexModifiable::RemoveRecord(const cGZPersistResourceKey *key, cGZDBPFIndexRecord *removed,
                                          uint32_t anyResource, uint32_t resourceZero)
{
    auto it = Find(key, static_cast<uint8_t>(anyResource), static_cast<uint8_t>(resourceZero));
    if (it == mMap.end()) return false;
    *removed = it->second;
    mMap.erase(it);
    return true;
}

void cGZDBPFIndexModifiable::SetRecordMemSize(const cGZPersistResourceKey *key, uint32_t memSize)
{
    cGZDBPFIndexRecord *record = FindRecordForWrite(key, 0, 0);
    if (record == nullptr) return;
    record->memSize = (record->memSize & kFlag) | (memSize & ~kFlag);
}

bool cGZDBPFIndexModifiable::AllEntriesWithin(uint32_t start, uint32_t end)
{
    for (const Node &n : mMap) {
        const uint32_t size = n.second.size;
        if (size == 0) continue;
        const uint32_t offset = n.second.offset;
        // Unsigned: an offset below start wraps to a large value, so it is outside [start, end) too.
        if (offset - start >= end - start) return false;
        if (size > end - offset) return false;
    }
    return true;
}

bool cGZDBPFIndexModifiable::IsRangeFree(uint32_t start, uint32_t length)
{
    if (length == 0) return true;
    const uint32_t end = start + length;
    for (const Node &n : mMap) {
        const uint32_t size = n.second.size;
        if (size == 0) continue;
        if (n.second.offset >= end) continue;
        if (start < n.second.offset + size) return false;
    }
    return true;
}

bool cGZDBPFIndexModifiable::SetEntries(const cGZDBPFIndexEntry *entries, uint32_t count, void *keyMap)
{
    for (uint32_t i = 0; i < count; ++i, ++entries) {
        cGZPersistResourceKey key{entries->type, entries->group, entries->instance, entries->resource};
        const uint32_t memSize = entries->memSize;
        if (keyMap != nullptr) VirtualCall<KeyMapFn, 4>(keyMap, &key, &key);
        Add(key, cGZDBPFIndexRecord{entries->offset, entries->size, memSize | kFlag});
    }
    return true;
}

void cGZDBPFIndexModifiable::CopyEntries(cGZDBPFIndexEntry *out, void *keyMap)
{
    for (const Node &n : mMap) {
        // The key is copied first; the key map may rewrite that copy (in place), and the copy is what is written out.
        cGZPersistResourceKey key = n.first;
        if (keyMap != nullptr) VirtualCall<KeyMapFn, 3>(keyMap, &key, &key);
        out->type = key.type;
        out->group = key.group;
        out->instance = key.instance;
        out->resource = key.resource;
        out->offset = n.second.offset;
        out->size = n.second.size;
        out->memSize = n.second.memSize;
        ++out;
    }
}

bool cGZDBPFIndexModifiable::SetEntriesFromIndex(const uint32_t *words, uint32_t count, void *keyMap,
                                                 uint32_t minorVersion)
{
    // The file's index: type, group, instance, [resource for minor version 2], offset, size. The memory size starts as the
    // stored size, flagged.
    for (uint32_t i = 0; i < count; ++i) {
        cGZPersistResourceKey key{words[0], words[1], words[2], 0};
        if (minorVersion == 2) {
            key.resource = words[3];
            words += 4;
        } else {
            words += 3;
        }
        if (keyMap != nullptr) VirtualCall<KeyMapFn, 4>(keyMap, &key, &key);
        const uint32_t offset = words[0], size = words[1];
        words += 2;
        Add(key, cGZDBPFIndexRecord{offset, size, size | kFlag});
    }
    return true;
}

void cGZDBPFIndexModifiable::WriteIndexEntries(std::vector<uint32_t> *out, void *keyMap, uint32_t minorVersion)
{
    const uint32_t count = static_cast<uint32_t>(mMap.size());
    if (minorVersion == 1)
        out->resize(count * 5);
    else if (minorVersion == 2)
        out->resize(count * 6);
    // Any other version writes five words per record into what `out` already holds.
    uint32_t *w = out->data();
    for (const Node &n : mMap) {
        cGZPersistResourceKey key = n.first;
        if (keyMap != nullptr) VirtualCall<KeyMapFn, 3>(keyMap, &key, &key);
        *w++ = key.type;
        *w++ = key.group;
        *w++ = key.instance;
        if (minorVersion == 2) *w++ = key.resource;
        *w++ = n.second.offset;
        *w++ = n.second.size;
    }
}

void cGZDBPFIndexModifiable::WriteCompressionDirectory(std::vector<uint32_t> *out, void *keyMap, uint32_t minorVersion)
{
    out->reserve(0x40);
    for (const Node &n : mMap) {
        // A record is compressed when it is smaller in the file than in memory.
        if (n.second.size >= (n.second.memSize & ~kFlag)) continue;
        cGZPersistResourceKey key = n.first;
        if (keyMap != nullptr) VirtualCall<KeyMapFn, 3>(keyMap, &key, &key);
        if (minorVersion == 1) {
            const uint32_t words[4] = {key.type, key.group, key.instance, n.second.memSize & ~kFlag};
            out->insert(out->end(), words, words + 4);
        } else if (minorVersion == 2) {
            const uint32_t words[5] = {key.type, key.group, key.instance, key.resource, n.second.memSize & ~kFlag};
            out->insert(out->end(), words, words + 5);
        }
    }
}

void cGZDBPFIndexModifiable::SetFlagOnAll()
{
    for (Node &n : mMap) n.second.memSize |= kFlag;
}

// ---- the RVA hooks' adapters ----
// Qualified calls, bodies forced inline so each adapter is the function itself.

using Adapters = cGZDBPFIndexModifiableAdapters;

Adapters::Self *__fastcall Adapters::Construct(Self *memory, void *)
{
    return ::new (memory) Self;
}

uint32_t __fastcall Adapters::GetTotalSize(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetTotalSize();
}

uint32_t __fastcall Adapters::GetSerializedSize(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetSerializedSize();
}

