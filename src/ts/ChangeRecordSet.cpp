#include "ts/ChangeRecordSet.h"

#include "game/original.h"
#include "game/vtable.h"
#include "rz/cRZVariant.h"

#include <new>

static_assert(sizeof(ChangeRecordSet::Map) == 0x20, "unordered_map layout");
static_assert(sizeof(std::pair<const ChangeRecordKey, ChangeRecordEntry>) + 8 == 0x20, "node 0x20");
static_assert(offsetof(ChangeRecordSet, mChanged) == 0x20 && sizeof(ChangeRecordSet) == 0x24, "change record set");

namespace {

void AddRef(void *object)
{
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(object);
}
void Release(void *object)
{
    VirtualCall<uint32_t(__thiscall *)(void *), 2>(object);
}

// The stream slots: cIGZIStream 13 GetUint32, 25 the resource key, 26 a serializable object (counted, into *out);
// cIGZOStream 37 SetUint32, 49 the resource key, 50 a serializable object.
uint8_t GetUint32(void *stream, uint32_t *out)
{
    return VirtualCall<uint8_t(__thiscall *)(void *, uint32_t *), 13>(stream, out);
}
uint8_t GetKey(void *stream, ChangeRecordKey *out)
{
    return VirtualCall<uint8_t(__thiscall *)(void *, ChangeRecordKey *), 25>(stream, out);
}
uint8_t GetObject(void *stream, void **out)
{
    return VirtualCall<uint8_t(__thiscall *)(void *, void **), 26>(stream, out);
}
uint8_t SetUint32(void *stream, uint32_t value)
{
    return VirtualCall<uint8_t(__thiscall *)(void *, uint32_t), 37>(stream, value);
}
uint8_t SetKey(void *stream, const ChangeRecordKey *key)
{
    return VirtualCall<uint8_t(__thiscall *)(void *, const ChangeRecordKey *), 49>(stream, key);
}
uint8_t SetObject(void *stream, void *object)
{
    return VirtualCall<uint8_t(__thiscall *)(void *, void *), 50>(stream, object);
}

} // namespace

ChangeRecordObject::~ChangeRecordObject()
{
    if (object) Release(object);
}

ChangeRecordSet::ChangeRecordSet() : mChanged(0) {}

void ChangeRecordSet::Erase(const ChangeRecordKey *key)
{
    if (mRecords.find(*key) == mRecords.end()) return;
    mRecords.erase(*key);
    mChanged = 1;
}

uint8_t ChangeRecordSet::Take(const ChangeRecordKey *key, uint32_t changeId, void **out)
{
    const auto it = mRecords.find(*key);
    if (it == mRecords.end()) return 0;
    if (it->second.changeId == changeId) {
        *out = it->second.record.object;
        AddRef(it->second.record.object);
        return 1;
    }
    mRecords.erase(it);
    return 0;
}

void ChangeRecordSet::Put(const ChangeRecordKey *key, uint32_t changeId, const void *variant)
{
    ChangeRecordEntry &entry = mRecords[*key];
    // A new cRZVariant (0x14 bytes) copied from `variant` (the cIGZVariant constructor, 0x9c7a40).
    void *copy = static_cast<cIGZVariant *>(
        ::new (::operator new(sizeof(cRZVariant))) cRZVariant(*static_cast<const cIGZVariant *>(variant)));
    void *old = entry.record.object;
    if (copy != old) {
        if (copy) AddRef(copy);
        entry.record.object = copy;
        if (old) Release(old);
    }
    entry.changeId = changeId;
    mChanged = 1;
}

uint8_t ChangeRecordSet::Read(void *stream)
{
    mRecords.clear();
    uint32_t version = 0;
    if (!GetUint32(stream, &version) || version != 1) return 0;
    uint32_t count = 0;
    if (!GetUint32(stream, &count)) return 0;
    while (count) {
        --count;
        ChangeRecordKey key{};
        if (!GetKey(stream, &key)) return 0;
        ChangeRecordEntry &entry = mRecords[key];
        if (!GetUint32(stream, &entry.changeId)) return 0;
        if (void *old = entry.record.object) {
            entry.record.object = nullptr;
            Release(old);
        }
        if (!GetObject(stream, &entry.record.object)) return 0;
    }
    return 1;
}

uint8_t ChangeRecordSet::Write(void *stream)
{
    if (!SetUint32(stream, 1) || !SetUint32(stream, static_cast<uint32_t>(mRecords.size()))) return 0;
    for (const auto &entry : mRecords) {
        if (!SetKey(stream, &entry.first) || !SetUint32(stream, entry.second.changeId) ||
            !SetObject(stream, entry.second.record.object))
            return 0;
    }
    mChanged = 0;
    return 1;
}

ChangeRecordSet *__fastcall ChangeRecordSetAdapters::Construct(ChangeRecordSet *self, void *)
{
    return new (self) ChangeRecordSet();
}
