#include "ts/cTSNeighborGeneticDataTable.h"
#include "app/AppDebugger.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/ComFramework.h"
#include "ifaces/NeighborDataContainer.h"
#include "ifaces/NeighborDbSegment.h"
#include "ifaces/NeighborGeneticData.h"
#include "ifaces/NeighborIdSource.h"
#include "ifaces/NeighborPersistSystem.h"
#include "ifaces/NeighborResourceManager.h"
#include "ifaces/NeighborStreamReader.h"
#include "ifaces/NeighborStreamWriter.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cITSNeighborGeneticDataTable.h"
#include "misc/UtilitiesC.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters.h"
#include "simantics/ServiceRefVector.h"
#include <cstring>
#include <new>
#include <string>
#include <tuple>

namespace {
constexpr uint32_t kUnknownInterface = 1;
constexpr uint32_t kTableInterfaceA = 0xe4fda3d4; // the table's own interface ids
constexpr uint32_t kTableInterfaceB = 0xec2391bf;
constexpr uint32_t kContainerInterface = 0x8db5bf48; // the cITSDataContainer face
constexpr uint32_t kPersistResourceInterface = 0x456b8f1d;
constexpr uint32_t kDbSegmentInterface = 0x656b8f0c;
constexpr uint32_t kDataInterface = 0x4bb0d269; // asked of the resource manager for the data objects
constexpr uint32_t kIdSourceInterface = 0x0db5c0c2; // what ExportTo/ImportFrom ask the container for
constexpr uint32_t kReaderInterface = 0x051b11bd;
constexpr uint32_t kWriterInterface = 0xc51b11cb;
constexpr uint32_t kNoId = 0xffffffff;
constexpr uint32_t kFormatVersion = 1;

// The persist system (cache 0x10faf6c, the getter 0xb390c0).
ifaces::NeighborPersistSystem *PersistSystem()
{
    return As<ifaces::NeighborPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}

// The cached resource manager (0x20deb5: not replaced yet).
ifaces::NeighborResourceManager *ResourceManager()
{
    return As<ifaces::NeighborResourceManager>(OriginalFunction<void *(__cdecl *)(), 0x20deb5>()());
}

// An object pointer held by a local variable of the original: released when it goes out of scope.
struct Held {
    void *object = nullptr;
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held() noexcept(false) { if (object) As<cIGZUnknown>(object)->Release(); }
    // The original releases a stale pointer before reusing the slot.
    void Reset()
    {
        void *old = object;
        object = nullptr;
        if (old) As<cIGZUnknown>(old)->Release();
    }
};

// A vector of 32-bit ids filled by the container through the allocator service (begin, end, capacity limit).
struct IdList : ServiceBlockVector {
    IdList() : ServiceBlockVector{} {}
    ~IdList() noexcept { FreeBlock(); }
    const uint32_t *Begin() const { return static_cast<const uint32_t *>(mFirst); }
    const uint32_t *End() const { return static_cast<const uint32_t *>(mLast); }
};

// The property name buffer of the copy loops: an empty std::string with zeroed small buffer. The data objects fill it
// with text allocated through the allocator service, so a heap buffer is freed the same way (0x152b80).
struct PropertyName {
    std::string text;
    PropertyName() { std::memset(text.data(), 0, 16); }
    PropertyName(const PropertyName &) = delete;
    ~PropertyName() noexcept
    {
        if (text.capacity() > 15) appdebugger::FreeStringBuffer(&text, text.data(), uint32_t(text.capacity()));
        new (&text) std::string();
    }
};

// Copies every (name, value) property of `from` into `to` (the first call starts the enumeration, the second continues
// it after the name). False when `from` has no property at all.
bool CopyProperties(void *from, void *to, uint32_t &value)
{
    PropertyName name;
    auto *source = As<ifaces::NeighborGeneticData>(from);
    if (!source->GetFirstProperty(&name.text, &value)) return false;
    do {
        As<ifaces::NeighborGeneticData>(to)->SetProperty(&name.text, value);
    } while (source->GetNextProperty(&name.text, &value));
    return true;
}
} // namespace

void NeighborDataRef::Hold(void *object)
{
    if (object) As<cIGZUnknown>(object)->AddRef();
}

void NeighborDataRef::Drop(void *object)
{
    if (object) As<cIGZUnknown>(object)->Release();
}

cTSNeighborGeneticDataTable *cTSNeighborGeneticDataTable::Create()
{
    auto *memory = static_cast<cTSNeighborGeneticDataTable *>(::operator new(sizeof(cTSNeighborGeneticDataTable)));
    if (!memory) return nullptr;
    try { return memory->Construct(); }
    catch (...) { ::operator delete(memory, sizeof(cTSNeighborGeneticDataTable)); throw; }
}

cTSNeighborGeneticDataTable *cTSNeighborGeneticDataTable::Construct()
{
    mVTable = GameAddress(kVTable);
    mContainer.mVTable = GameAddress(kContainerVTable);
    mPersistVTable = GameAddress(kPersistVTable);
    mRefCount = 0;
    for (auto &word : mKey) word = 0;
    mModified = 0;
    new (&mData) NeighborDataMap;
    return this;
}

uint8_t cTSNeighborGeneticDataTable::QueryInterface(uint32_t id, void **out)
{
    void *face;
    switch (id) {
    case kUnknownInterface:
    case kTableInterfaceA:
    case kTableInterfaceB: face = this; break;
    case kPersistResourceInterface: face = &mPersistVTable; break;
    case kContainerInterface: face = &mContainer; break;
    default: return 0;
    }
    *out = face;
    As<ifaces::cITSNeighborGeneticDataTable>(this)->AddRef();
    return 1;
}

void cTSNeighborGeneticDataTable::Destroy() noexcept
{
    mVTable = GameAddress(kVTable);
    mContainer.mVTable = GameAddress(kContainerVTable);
    mPersistVTable = GameAddress(kPersistVTable);
    mData.~NeighborDataMap();
}

uint32_t cTSNeighborGeneticDataTable::Release()
{
    if (mRefCount > 1) return --mRefCount;
    // The last reference: no virtual destructor, the table destroys itself (data objects released) and frees its memory.
    Destroy();
    ::operator delete(this, sizeof(*this));
    return 0;
}

uint32_t cTSNeighborGeneticDataTable::GetGZCLSID()
{
    return kClassId;
}

uint8_t cTSNeighborGeneticDataTable::Write(void *stream)
{
    auto *out = As<ifaces::NeighborStreamWriter>(stream);
    out->SetSint32(kFormatVersion);
    auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
    if (metadata) metadata->Construct(kFormatVersion);
    out->SetMetadata(metadata);
    out->SetSint32(uint32_t(mData.size()));
    uint8_t ok = 1;
    auto *persist = PersistSystem();
    if (!persist) return 0;
    for (auto entry = mData.begin(); entry != mData.end(); ++entry) {
        const uint32_t id = entry->first;
        out->SetSint32(id);
        NeighborDataRef data(entry->second.Get());
        uint32_t resourceKey[4] = {kDataResourceType, mKey[1], id, 0};
        Held resource;
        // ORIGINAL-BUG: a null data object (Set(id, null), or a Read whose lookup hands out null) is queried through its
        // null vtable here and crashes.
        if (As<ifaces::NeighborGeneticData>(data.Get())->QueryInterface(kPersistResourceInterface, &resource.object)) {
            // The data object is saved under this table's group and the id, then gets its own key back.
            uint32_t ownKey[4];
            auto *face = As<ifaces::cIGZPersistResource>(resource.object);
            face->GetKey(ownKey);
            face->SetKey(resourceKey);
            ok &= persist->SaveResource(resource.object, nullptr);
            face->SetKey(ownKey);
        }
    }
    return ok;
}

uint8_t cTSNeighborGeneticDataTable::Read(void *stream)
{
    auto *in = As<ifaces::NeighborStreamReader>(stream);
    // The original leaves all three locals unwritten when a read fails (stack bytes); a failing read leaves these as they are.
    uint32_t version = 0, count = 0, id = 0;
    in->GetSint32(&version);
    in->GetSint32(&count);
    auto *persist = PersistSystem();
    if (!persist) return 0;
    for (uint32_t index = 0; index < count; ++index) {
        in->GetSint32(&id);
        Held data;
        uint32_t resourceKey[4] = {kDataResourceType, mKey[1], id, 0};
        if (persist->GetResource(resourceKey, kDataInterface, &data.object, 0, 0)) {
            // The id is added only when it is new; a counted copy goes through the insert either way.
            NeighborDataRef pending(data.object);
            if (mData.find(id) == mData.end())
                mData.emplace(std::piecewise_construct, std::forward_as_tuple(id), std::forward_as_tuple(pending));
        }
    }
    return 1;
}

uint8_t cTSNeighborGeneticDataTable::Clear()
{
    mData.clear();
    mModified = 0;
    return 1;
}

uint8_t cTSNeighborGeneticDataTable::Save()
{
    uint8_t saved = 0;
    Held segment;
    if (auto *persist = PersistSystem()) {
        uint32_t keyCopy[4];
        void *key = As<ifaces::cIGZPersistResource>(&mPersistVTable)->GetKey(keyCopy);
        if (persist->FindSegment(key, kDbSegmentInterface, &segment.object)) {
            saved = persist->SaveResource(&mPersistVTable, segment.object);
            saved &= As<ifaces::NeighborDbSegment>(segment.object)->Flush();
            mModified = 0;
        }
    }
    return saved;
}

uint8_t cTSNeighborGeneticDataTable::Set(uint32_t id, void *data)
{
    mModified = 1;
    auto entry = mData.find(id);
    if (entry != mData.end()) {
        entry->second.Assign(data);
        return 1;
    }
    // A counted copy goes into the insert, which counts the new entry's own reference; the copy is released afterwards.
    NeighborDataRef pending(data);
    mData.emplace(std::piecewise_construct, std::forward_as_tuple(id), std::forward_as_tuple(pending));
    return 0;
}

uint8_t cTSNeighborGeneticDataTable::Remove(uint32_t id)
{
    auto entry = mData.find(id);
    if (entry == mData.end()) return 0;
    mModified = 1;
    // The node handle releases the data object, then frees the node.
    auto removed = mData.extract(entry);
    return 1;
}

void cTSNeighborGeneticDataTable::SetModified()
{
    mModified = 1;
}

void *cTSNeighborGeneticDataTable::Get(uint32_t id)
{
    auto entry = mData.find(id);
    return entry == mData.end() ? nullptr : entry->second.Get();
}

uint32_t cTSNeighborGeneticDataTable::FirstId()
{
    auto first = mData.begin();
    return first == mData.end() ? kNoId : first->first;
}

uint32_t cTSNeighborGeneticDataTable::NextId(uint32_t id)
{
    for (auto entry = mData.begin(); entry != mData.end(); ++entry) {
        if (entry->first != id) continue;
        ++entry;
        return entry == mData.end() ? kNoId : entry->first;
    }
    return kNoId;
}

void *cTSNeighborGeneticDataTable::CreateData()
{
    Held created;
    auto *manager = ResourceManager();
    if (!manager) return nullptr;
    manager->CreateResource(kDataResourceType, kDataInterface, &created.object, 0, 0);
    return created.object;
}

void *cTSNeighborGeneticDataTable::LoadOrCreate(uint32_t id)
{
    Held found;
    auto *manager = ResourceManager();
    if (!manager) return nullptr;
    uint32_t resourceKey[4] = {kDataResourceType, mKey[1], id, 0};
    if (!manager->GetResource(resourceKey, kDataInterface, &found.object, 0, 0)) {
        found.Reset();
        manager->CreateResource(kDataResourceType, kDataInterface, &found.object, 0, 0);
    }
    void *data = found.object;
    if (data) {
        auto entry = mData.find(id);
        if (entry == mData.end())
            entry = mData.emplace(std::piecewise_construct, std::forward_as_tuple(id), std::forward_as_tuple()).first;
        entry->second.Assign(data);
    }
    return data;
}

uint8_t cTSNeighborGeneticDataTable::ReadFromHolder(void *holder)
{
    Held reader;
    if (!As<ifaces::NeighborDataContainer>(holder)->QueryInterface(kReaderInterface, &reader.object)) return 0;
    return As<ifaces::cITSNeighborGeneticDataTable>(this)->Read(reader.object);
}

uint8_t cTSNeighborGeneticDataTable::WriteToHolder(void *holder)
{
    Held writer;
    if (!As<ifaces::NeighborDataContainer>(holder)->QueryInterface(kWriterInterface, &writer.object)) return 0;
    return As<ifaces::cITSNeighborGeneticDataTable>(this)->Write(writer.object);
}

uint8_t NeighborContainerFace::ExportTo(void *container)
{
    uint8_t saved = 0;
    if (!container) return 0;
    auto *table = As<ifaces::cITSNeighborGeneticDataTable>(Owner());
    auto *source = As<ifaces::NeighborDataContainer>(container);
    Held idSource;
    if (!source->QueryInterface(kIdSourceInterface, &idSource.object)) return 0;
    Held segment;
    if (source->GetInterface(kDbSegmentInterface, &segment.object)) {
        IdList ids;
        As<ifaces::NeighborIdSource>(idSource.object)->GetIds(&ids);
        Held copy;
        As<ifaces::ComFramework>(GetDirectorFrameworkObject())->GetClassObject(cTSNeighborGeneticDataTable::kClassId, kTableInterfaceB, &copy.object);
        // The new table's key: its class, the container's group and instance 1.
        uint32_t copyKey[4] = {cTSNeighborGeneticDataTable::kClassId, source->GetGroup(), 1, 0};
        // The creation result is not checked: a failed creation faults here.
        auto *newTable = As<ifaces::cITSNeighborGeneticDataTable>(copy.object);
        As<ifaces::cIGZPersistResource>(newTable->GetPersistResource())->SetKey(copyKey);
        // The first property value slot is the stack slot that held `this` on entry.
        uint32_t value = uint32_t(reinterpret_cast<uintptr_t>(this));
        for (const uint32_t *entry = ids.Begin(); entry != ids.End(); ++entry) {
            const uint32_t id = *entry;
            void *data = table->Get(id);
            if (!data) continue;
            void *duplicate = newTable->CreateData();
            CopyProperties(data, duplicate, value);
            newTable->Set(id, duplicate); // stored even when the data object had no property
        }
        auto *persist = PersistSystem();
        saved = persist->SaveResource(newTable->GetPersistResource(), segment.object);
    }
    return saved;
}

uint8_t NeighborContainerFace::ImportFrom(void *container)
{
    uint8_t result = 0;
    if (!container) return 0;
    auto *table = As<ifaces::cITSNeighborGeneticDataTable>(Owner());
    auto *source = As<ifaces::NeighborDataContainer>(container);
    Held idSource;
    if (!source->QueryInterface(kIdSourceInterface, &idSource.object)) return 0;
    Held segment;
    if (source->GetInterface(kDbSegmentInterface, &segment.object)) {
        Held loaded;
        // The table is stored as instance 1 of the container's group, or else as instance 0.
        const uint32_t group = source->GetGroup();
        uint32_t firstKey[4] = {cTSNeighborGeneticDataTable::kClassId, group, 1, 0};
        uint32_t secondKey[4] = {cTSNeighborGeneticDataTable::kClassId, group, 0, 0};
        auto *persist = PersistSystem();
        bool found = persist->GetResource(firstKey, kTableInterfaceB, &loaded.object, 0, 0);
        if (!found) {
            loaded.Reset();
            found = persist->GetResource(secondKey, kTableInterfaceB, &loaded.object, 0, 0);
        }
        if (found) {
            result = 1;
            IdList ids;
            As<ifaces::NeighborIdSource>(idSource.object)->GetIds(&ids);
            uint32_t value = 0; // never written before the first successful GetFirstProperty
            for (const uint32_t *entry = ids.Begin(); entry != ids.End(); ++entry) {
                const uint32_t id = *entry;
                uint32_t mapped = 0;
                const uint8_t mappedOk = As<ifaces::NeighborIdSource>(idSource.object)->MapId(id, &mapped);
                result &= mappedOk;
                if (!mappedOk) continue;
                void *duplicate = table->CreateData();
                void *original = As<ifaces::cITSNeighborGeneticDataTable>(loaded.object)->Get(id);
                if (original && CopyProperties(original, duplicate, value)) table->Set(mapped, duplicate);
            }
        }
    }
    return result;
}
