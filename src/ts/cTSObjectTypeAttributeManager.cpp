#include "ts/cTSObjectTypeAttributeManager.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIEdithObjectTypeAttribute.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cITSObjectTypeAttributeManager.h"
#include "ifaces/cITSPersistSystem.h"
#include "misc/UtilitiesC.h"
#include "rz/ServiceGetters.h"

#include <cstring>
#include <new>

namespace {

constexpr uint32_t kSegmentIid = 0x656b8f0c; // cIGZPersistDBSegment, asked of FindSegment

ifaces::cITSPersistSystem *PersistSystem()
{
    return As<ifaces::cITSPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}

// The stream metadata object both directions hand to the stream (slot 19), which takes it over.
UtilObjectB *NewMetadata(uint32_t version)
{
    auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
    if (metadata) metadata = metadata->Construct(version);
    return metadata;
}

// A counted object the persist system hands out, released when the holder goes.
struct HeldSegment {
    void *object = nullptr;
    HeldSegment() = default;
    HeldSegment(const HeldSegment &) = delete;
    HeldSegment &operator=(const HeldSegment &) = delete;
    ~HeldSegment()
    {
        if (object) As<cIGZUnknown>(object)->Release();
    }
};

// The other manager as AddAllFrom gets it from QueryInterface, released when the holder goes.
struct HeldManager {
    cTSObjectTypeAttributeManager *object = nullptr;
    HeldManager() = default;
    HeldManager(const HeldManager &) = delete;
    HeldManager &operator=(const HeldManager &) = delete;
    ~HeldManager()
    {
        if (object) As<cIGZUnknown>(object)->Release();
    }
};

} // namespace

// ---- the counted attribute ----

ObjectTypeAttributeRef::ObjectTypeAttributeRef(ifaces::cIEdithObjectTypeAttribute *object) : mObject(object)
{
    if (mObject) As<cIGZUnknown>(mObject)->AddRef();
}

ObjectTypeAttributeRef::ObjectTypeAttributeRef(const ObjectTypeAttributeRef &other) : mObject(other.mObject)
{
    if (mObject) As<cIGZUnknown>(mObject)->AddRef();
}

ObjectTypeAttributeRef::~ObjectTypeAttributeRef()
{
    if (mObject) As<cIGZUnknown>(mObject)->Release();
}

void ObjectTypeAttributeRef::Assign(ifaces::cIEdithObjectTypeAttribute *object)
{
    ifaces::cIEdithObjectTypeAttribute *old = mObject;
    if (object == old) return;
    if (object) As<cIGZUnknown>(object)->AddRef();
    mObject = object;
    if (old) As<cIGZUnknown>(old)->Release();
}

// ---- construction and destruction ----

cTSObjectTypeAttributeManager::cTSObjectTypeAttributeManager() : mRefCount(0), mAttributes(), mKey{} {}

// Out of line, not defaulted: the compiler's destructor then stores both vtables before destroying the map, as the original's
// (docs/standalone-shape.md, cTSFamilyHistory).
cTSObjectTypeAttributeManager::~cTSObjectTypeAttributeManager() {}

cTSObjectTypeAttributeManager *cTSObjectTypeAttributeManager::Create() { return new (std::nothrow) cTSObjectTypeAttributeManager; }

bool cTSObjectTypeAttributeManager::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case 1:
    case kInterfaceId:
    case kClassInterfaceId:
    case kSerializableId:
        *out = static_cast<cITSObjectTypeAttributeManager *>(this);
        AddRef();
        return true;
    case kPersistResourceId:
        *out = static_cast<cIGZPersistResource *>(this);
        AddRef();
        return true;
    default:
        return false;
    }
}

uint32_t cTSObjectTypeAttributeManager::Release()
{
    if (--mRefCount == 0) {
        delete this;
        return 0;
    }
    return mRefCount;
}

// ---- cIGZSerializable ----

bool cTSObjectTypeAttributeManager::Write(ifaces::cIGZOStream *stream)
{
    for (uint32_t i = 0; i < kHeaderBytes; ++i) stream->SetUint8(0);
    stream->SetUint32(GetGZCLSID());
    stream->SetUint32(kRecordVersion);
    stream->SetUint32(0);
    stream->SetMetadata(NewMetadata(kRecordVersion));
    stream->SetUint32(uint32_t(mAttributes.size()));
    for (const auto &entry : mAttributes) {
        ifaces::cIEdithObjectTypeAttribute *attribute = entry.second.Get();
        stream->SetUint32(attribute->GetId());
        const uint32_t count = attribute->GetCount();
        const uint16_t *values = attribute->GetData();
        stream->SetUint32(count);
        for (uint32_t i = 0; i < count; ++i) stream->SetUint16(values[i]);
    }
    return !stream->GetError();
}

bool cTSObjectTypeAttributeManager::Read(ifaces::cIGZIStream *stream)
{
    // Every word below is a stack local the stream fills; a failing read leaves it as it was (uninitialised in the original).
    uint32_t word;
    uint32_t version;
    stream->Skip(kHeaderBytes);
    stream->GetUint32(&word); // the class id, not checked
    stream->GetUint32(&version);
    stream->GetUint32(&word); // the zero word
    stream->SetMetadata(NewMetadata(version));
    uint32_t entries;
    stream->GetUint32(&entries);
    uint16_t scratch[kScratchValues]; // where the values of an unknown id go
    for (uint32_t entry = 0; entry < entries; ++entry) {
        uint32_t id;
        stream->GetUint32(&id);
        ifaces::cIEdithObjectTypeAttribute *attribute = FindAttributes(id);
        uint32_t capacity;
        uint16_t *values;
        if (attribute) {
            capacity = attribute->GetCount();
            values = attribute->GetData();
        } else {
            capacity = kScratchValues;
            values = scratch;
        }
        uint32_t count;
        stream->GetUint32(&count);
        if (count > capacity) return false;
        uint32_t i = 0;
        for (; i < count; ++i) stream->GetSint16(reinterpret_cast<int16_t *>(&values[i]));
        if (i < capacity) std::memset(&values[i], 0, (capacity - i) * sizeof(uint16_t));
    }
    return !stream->GetError();
}

uint32_t cTSObjectTypeAttributeManager::GetGZCLSID() { return kClassId; }

// ---- cITSObjectTypeAttributeManager ----

ifaces::cIEdithObjectTypeAttribute *cTSObjectTypeAttributeManager::FindAttributes(uint32_t id)
{
    const auto found = mAttributes.find(id);
    return found != mAttributes.end() ? found->second.Get() : nullptr;
}

uint8_t cTSObjectTypeAttributeManager::AddAttributes(ifaces::cIEdithObjectTypeAttribute *attributes)
{
    const auto found = mAttributes.find(attributes->GetId());
    if (found != mAttributes.end()) {
        found->second.Assign(attributes);
        return 1;
    }
    // The id is asked again for the inserted pair (a counted copy of the attribute, released after the insert counted its own).
    const ObjectTypeAttributeMap::value_type entry(attributes->GetId(), attributes);
    return mAttributes.insert(entry).second;
}

void cTSObjectTypeAttributeManager::AddAllFrom(cIGZUnknown *other)
{
    HeldManager source;
    if (other->QueryInterface(kClassInterfaceId, reinterpret_cast<void **>(&source.object))) {
        ObjectTypeAttributeMap &attributes = source.object->mAttributes;
        for (auto it = attributes.begin(); it != attributes.end(); ++it)
            AddAttributes(it->second.Get());
    }
}

uint8_t cTSObjectTypeAttributeManager::Save()
{
    ifaces::cITSPersistSystem *persist = PersistSystem();
    HeldSegment segment;
    // The key through the persist face's slot 3 (this + 4, no null test), the resource as that face (null-tested).
    const cGZPersistResourceKey key = GetKey();
    if (!persist->FindSegment(&key, kSegmentIid, &segment.object)) return 0;
    const uint8_t saved = persist->SaveResource(As<ifaces::cIGZPersistResource>(static_cast<cIGZPersistResource *>(this)), segment.object) & 1;
    As<ifaces::cIGZPersistDBSegment>(segment.object)->Flush(); // its answer is not used
    return saved;
}

void cTSObjectTypeAttributeManager::Clear() { mAttributes.clear(); }

// ---- the RVA hooks' adapters (one statement each: matchcheck v7 ties them to the member in our vtable) ----

using Adapters = cTSObjectTypeAttributeManagerAdapters;

// 0x4d8a30: the deleting destructor (slot 12; game callers: none but the vtable).
Adapters::Self *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}
bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}
uint32_t __fastcall Adapters::GetGZCLSID(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetGZCLSID();
}
void __fastcall Adapters::Clear(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Clear();
}
