#include "ts/cTSRelationshipTable.h"
#include "ts/cTSRelationshipEntry.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/cITSRelationshipEntry.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZPersistDBRecord.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cITSPersistSystem.h"
#include "rz/ServiceGetters.h"
#include <new>
#include <tuple>
#include <utility>

namespace {
struct ResourceKey { uint32_t type, group, instance, instanceHigh; };
static_assert(sizeof(ResourceKey) == 16);
const cGZPersistResourceKey *AsKey(const ResourceKey *key) { return reinterpret_cast<const cGZPersistResourceKey *>(key); }

struct TemporaryReference {
    void *value = nullptr;
    ~TemporaryReference() noexcept { if (value) As<cIGZUnknown>(value)->Release(); }
    void Clear() {
        void *old = value;
        value = nullptr;
        if (old) As<cIGZUnknown>(old)->Release();
    }
};

// A new entry (ts/cTSRelationshipEntry.h, our vtables) holding one reference (its AddRef), as the table's callers expect.
__forceinline cTSRelationshipEntry *CreateEntry(const ResourceKey &key)
{
    auto *entry = new cTSRelationshipEntry(*AsKey(&key));
    entry->AddRef();
    return entry;
}
// Entries already in the map are other objects (ours, or fakes in the tests): called through the generated view.
__forceinline ifaces::cITSRelationshipEntry *Entry(void *entry) { return As<ifaces::cITSRelationshipEntry>(entry); }
__forceinline ifaces::cITSPersistSystem *PersistSystem()
{
    return As<ifaces::cITSPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}
// The cached resource manager (0x20deb5: not replaced yet).
__forceinline ifaces::cIGZPersistResourceManager *ResourceManager()
{
    return As<ifaces::cIGZPersistResourceManager>(OriginalFunction<void *(__cdecl *)(), 0x20deb5>()());
}
// Group of records read from pre-version-3 streams.
constexpr uint32_t kLegacyGroup = 0xac3a7122;
}

RelationshipEntryRef::RelationshipEntryRef(void *entry) : value(entry)
{
    if (value) As<cIGZUnknown>(value)->AddRef();
}
RelationshipEntryRef::RelationshipEntryRef(const RelationshipEntryRef &other) : RelationshipEntryRef(other.value) {}
RelationshipEntryRef::~RelationshipEntryRef() noexcept
{
    if (value) As<cIGZUnknown>(value)->Release();
}
RelationshipEntryRef &RelationshipEntryRef::operator=(const RelationshipEntryRef &other)
{
    if (this != &other) Assign(other.value);
    return *this;
}
void RelationshipEntryRef::Assign(void *entry)
{
    void *old = value;
    if (entry == old) return;
    if (entry) As<cIGZUnknown>(entry)->AddRef();
    value = entry;
    if (old) As<cIGZUnknown>(old)->Release();
}
void RelationshipEntryRef::Reset() noexcept
{
    void *old = value;
    if (!old) return;
    value = nullptr;
    As<cIGZUnknown>(old)->Release();
}

// ---- construction and destruction ----

// 0x55ab10: the compiler installs our vtables (the +4 interface's own first, as the original's dead store); the fields in
// declaration order, the map's eight buckets, the storage last.
cTSRelationshipTable::cTSRelationshipTable() : mEntries() {}

// The scalar deleting destructor (slot 14, 0x55abf0) is the compiler's: this destructor (the class's vtables stored back, the
// storage released, the map destroyed with its entries' references), then operator delete(this, 0x40) on bit 0; returns this.
cTSRelationshipTable::~cTSRelationshipTable()
{
    if (mStorage) As<cIGZUnknown>(mStorage)->Release();
}

// ---- cIGZUnknown ----

// 0x55b310: this for 1 and the table's id (no null mask), the +4 face for cIGZSerializable (with the null test of a base-class
// conversion); retained through slot 1.
bool cTSRelationshipTable::QueryInterface(uint32_t id, void **out)
{
    if (id == 1 || id == kInterfaceId) *out = this;
    else if (id == kSerializableId) *out = static_cast<cIGZSerializable *>(this);
    else return false;
    AddRef();
    return true;
}

// slot 1 (folded 0x145260)
uint32_t cTSRelationshipTable::AddRef()
{
    return ++mRefCount;
}

// 0x55baf0
uint32_t cTSRelationshipTable::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this; // slot 14 with 1
    return 0;
}

// ---- cIGZSerializable face ----

// 0x55b000
uint32_t cTSRelationshipTable::GetGZCLSID()
{
    return kClassId;
}

// 0x55c170
bool cTSRelationshipTable::Write(ifaces::cIGZOStream *out)
{
    void *const stream = out;
    TemporaryReference record;
    ResourceKey context{};
    TemporaryReference segment;
    uint32_t type, group;
    if (As<cIGZUnknown>(stream)->QueryInterface(0xe56b8f03, &record.value)) {
        context = *static_cast<ResourceKey *>(As<ifaces::cIGZPersistDBRecord>(record.value)->GetKey());
        PersistSystem()->FindSegment(AsKey(&context), 0x656b8f0c, &segment.value);
        type = GetGZCLSID(); // through the +4 face's slot 5
        group = context.group;
    } else {
        // ORIGINAL-BUG: without the keyed stream interface, the stream address
        // becomes both the resource type and group of each loaded entry.
        type = group = uint32_t(reinterpret_cast<uintptr_t>(stream));
    }
    out->SetSint32(3);
    out->SetUint32(uint32_t(mEntries.size()));
    for (const auto &entry : mEntries)
        out->SetUint32(entry.first);
    for (const auto &entry : mEntries) {
        RelationshipEntryRef retained(entry.second.value);
        if (!retained.value) continue;
        ResourceKey key;
        Entry(retained.value)->GetPersistResource()->GetKey(&key);
        key.type = type;
        key.group = group;
        // The original fetches the resource interface twice, in this order.
        Entry(retained.value)->GetPersistResource()->SetKey(&key);
        if (!segment.value) continue;
        auto *storage = As<ifaces::cIGZPersistDBSegment>(segment.value);
        TemporaryReference output;
        bool opened = storage->OpenRecord(AsKey(&key), &output.value, 2) != 0;
        if (!opened) {
            output.Clear();
            opened = storage->CreateNewRecord(AsKey(&key), &output.value) != 0;
        }
        if (opened) Entry(retained.value)->WriteToRecord(output.value);
        if (output.value) {
            storage->CloseRecord(static_cast<ifaces::cIGZPersistDBRecord *>(output.value));
            storage->Flush();
        }
    }
    return true;
}

// 0x55b420
bool cTSRelationshipTable::Read(ifaces::cIGZIStream *in)
{
    void *const stream = in;
    if (mEntries.size() != 0) mEntries.clear();
    int32_t version;
    in->GetSint32(&version);
    TemporaryReference record;
    ResourceKey key{};
    if (As<cIGZUnknown>(stream)->QueryInterface(0xe56b8f03, &record.value)) {
        key = *static_cast<ResourceKey *>(As<ifaces::cIGZPersistDBRecord>(record.value)->GetKey());
        auto *manager = ResourceManager();
        if (void *old = mStorage) {
            mStorage = nullptr;
            As<cIGZUnknown>(old)->Release();
        }
        manager->FindSegmentWithKey(AsKey(&key), &mStorage);
        if (version == 3) {
            mResourceType = GetGZCLSID(); // through the +4 face's slot 5
            mResourceGroup = key.group;
        } else {
            mResourceType = key.type;
            mResourceGroup = kLegacyGroup;
        }
    }
    if (version < 0) {
        // Old format: the entries inline, one flagged slot per person index.
        int32_t count;
        in->GetSint32(&count);
        for (int32_t slot = 0; slot < count; ++slot) {
            int32_t present;
            in->GetSint32(&present);
            if (!present) {
                mEntries[uint32_t(slot)].Reset();
                continue;
            }
            uint32_t person;
            in->GetUint32(&person);
            key.instance = person;
            TemporaryReference entry;
            cTSRelationshipEntry *created = CreateEntry(key);
            entry.value = created;
            created->Read(in); // through the entry's +8 face, slot 4
            mEntries[person].Assign(entry.value);
        }
    } else if (version == 2 || version == 3) {
        // Keys only: each entry loads from its own record on first use (LoadEntry).
        uint32_t count;
        in->GetUint32(&count);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t person;
            in->GetUint32(&person);
            mEntries[person].Reset();
        }
    } else {
        return false;
    }
    return true;
}

// ---- cITSRelationshipTable ----

// 0x55bec0: the result of the +4 face's Write is passed through as it is (all of eax).
bool cTSRelationshipTable::WriteForPerson(ifaces::cIGZOStream *stream, uint32_t person)
{
    mOwnerPerson = person;
    return Write(stream); // through the +4 face's vtable (this is never null: no null test)
}
// 0x55b2a0
bool cTSRelationshipTable::ReadForPerson(ifaces::cIGZIStream *stream, uint32_t person)
{
    mOwnerPerson = person;
    return Read(stream);
}
// slot 11 (folded 0x15e970)
uint32_t cTSRelationshipTable::GetPersonCount()
{
    return uint32_t(mEntries.size());
}
// 0x55afd0
uint32_t cTSRelationshipTable::GetFirstPerson()
{
    return mEntries.empty() ? UINT32_MAX : mEntries.begin()->first;
}
// 0x55b010
uint32_t cTSRelationshipTable::GetNextPerson(uint32_t person)
{
    auto position = mEntries.find(person);
    if (position == mEntries.end() || ++position == mEntries.end()) return UINT32_MAX;
    return position->first;
}
// slot 15 (folded 0x46e120): the new storage retained, then the old one released; nothing when they are the same.
void cTSRelationshipTable::SetStorage(cIGZUnknown *storage)
{
    void *const old = mStorage;
    if (storage == old) return;
    if (storage) storage->AddRef();
    mStorage = storage;
    if (old) As<cIGZUnknown>(old)->Release();
}
// slots 16 and 17 (folded 0x156110, 0x1b9690)
bool cTSRelationshipTable::IsModified()
{
    return mModified;
}
void cTSRelationshipTable::SetModified(bool modified)
{
    mModified = modified;
}

// 0x55ac60
void cTSRelationshipTable::SetEntry(uint32_t owner, uint32_t person, RelationshipMap::iterator *out)
{
    mOwnerPerson = owner;
    TemporaryReference entry;
    entry.value = CreateEntry({mResourceType, mResourceGroup, mOwnerPerson << 16 | person, 0});
    std::pair<RelationshipMap::iterator, bool> inserted;
    {
        const RelationshipMap::value_type value(std::piecewise_construct, std::forward_as_tuple(person),
            std::forward_as_tuple(entry.value));
        inserted = mEntries.insert(value);
    }
    if (!inserted.second) inserted.first->second.Assign(entry.value);
    *out = inserted.first;
}
// 0x55b090
void *cTSRelationshipTable::LoadEntry(RelationshipMap::iterator position)
{
    if (position == mEntries.end()) return nullptr;
    void *entry = position->second.value;
    if (entry) return entry;
    SetEntry(mOwnerPerson, position->first, &position);
    entry = position->second.value;
    RelationshipEntryRef retained(entry);
    TemporaryReference record;
    if (mStorage) {
        ResourceKey key;
        Entry(entry)->GetPersistResource()->GetKey(&key);
        if (As<ifaces::cIGZPersistDBSegment>(mStorage)->OpenRecord(AsKey(&key), &record.value, 1)) {
            if (entry) Entry(entry)->ReadFromRecord(record.value);
            As<ifaces::cIGZPersistDBRecord>(record.value)->Close();
        }
    }
    return entry;
}
// 0x55aef0
uint32_t cTSRelationshipTable::GetValueCount(uint32_t person)
{
    auto position = mEntries.find(person);
    if (position == mEntries.end()) return 0;
    RelationshipEntryRef entry(LoadEntry(position));
    if (!entry.value) return 0;
    return Entry(entry.value)->GetValueCount();
}
// 0x55bf70
bool cTSRelationshipTable::SetValueCount(uint32_t owner, uint32_t person, uint32_t count)
{
    auto position = mEntries.find(person);
    if (position == mEntries.end()) {
        SetEntry(owner, person, &position);
        if (position == mEntries.end()) return false;
    }
    RelationshipEntryRef entry(LoadEntry(position));
    if (!entry.value) return false;
    mModified = 1;
    return Entry(entry.value)->SetValueCount(count);
}
// 0x55b1b0
uint32_t cTSRelationshipTable::GetValue(uint32_t person, uint32_t index)
{
    auto position = mEntries.find(person);
    if (position == mEntries.end()) return 0;
    RelationshipEntryRef entry(LoadEntry(position));
    if (!entry.value) return 0;
    return Entry(entry.value)->GetValue(index);
}
// 0x55c080
bool cTSRelationshipTable::SetValue(uint32_t person, uint32_t index, uint32_t value)
{
    auto position = mEntries.find(person);
    if (position == mEntries.end()) return false;
    RelationshipEntryRef entry(LoadEntry(position));
    if (!entry.value) return false;
    mModified = 1;
    return Entry(entry.value)->SetValue(index, value);
}
// 0x55bbc0
void cTSRelationshipTable::RemovePerson(uint32_t person)
{
    auto position = mEntries.find(person);
    if (position == mEntries.end()) return;
    if (mResourceType && mResourceGroup) {
        ResourceKey key{mResourceType, mResourceGroup, mOwnerPerson << 16 | person, 0};
        PersistSystem()->DeleteResource(&key);
    }
    mEntries.erase(position);
    mModified = 1;
}
// 0x55bb10
void cTSRelationshipTable::Clear(bool deleteResources)
{
    if (deleteResources && mResourceType && mResourceGroup) {
        for (const auto &entry : mEntries) {
            ResourceKey key{mResourceType, mResourceGroup, mOwnerPerson << 16 | entry.first, 0};
            PersistSystem()->DeleteResource(&key);
        }
    }
    if (mEntries.size() != 0) mEntries.clear();
    mModified = 1;
}

// ---- the RVA hooks' adapters ----
// Qualified calls (no virtual dispatch: a table built with the game's vtables would reach these very hooks again), bodies forced
// inline so each adapter is the function itself (matchcheck compares the adapter). A base-to-derived static_cast finds the
// object from a face.

using Adapters = cTSRelationshipTableAdapters;

// The constructor hook: every table the game builds (cTSNeighbor::Create is ours and builds it with new; the game's 0x52c4b0
// calls this) gets our vtables.
Adapters::Self *__fastcall Adapters::Construct(void *memory, void *)
{
    return ::new (memory) Self;
}

uint32_t __fastcall Adapters::Release(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Release();
}

bool __fastcall Adapters::WriteForPerson(Self *self, void *, ifaces::cIGZOStream *stream, uint32_t person)
{
    [[msvc::forceinline_calls]] return self->Self::WriteForPerson(stream, person);
}

bool __fastcall Adapters::ReadForPerson(Self *self, void *, ifaces::cIGZIStream *stream, uint32_t person)
{
    [[msvc::forceinline_calls]] return self->Self::ReadForPerson(stream, person);
}

uint32_t __fastcall Adapters::GetGZCLSID(cIGZSerializable *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetGZCLSID();
}

void *__fastcall Adapters::LoadEntry(Self *self, void *, RelationshipMap::iterator position)
{
    [[msvc::forceinline_calls]] return self->Self::LoadEntry(position);
}

void __fastcall Adapters::SetEntry(Self *self, void *, uint32_t owner, uint32_t person, RelationshipMap::iterator *out)
{
    [[msvc::forceinline_calls]] self->Self::SetEntry(owner, person, out);
}
