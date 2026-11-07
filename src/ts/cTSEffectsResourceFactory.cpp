#include "ts/cTSEffectsResourceFactory.h"

#include "gz/cGZPersistResource.h"
#include "gz/cGZPersistResourceFactory.h"
#include "ts/cTSEffectsResource.h"

#include <new>

// cTSEffectsResourceFactory (lane IT). Slots 0-6 are the base factory's members (ours, old shape, the same layout) run on
// this object.

namespace {

cGZPersistResourceFactory *Base(cTSEffectsResourceFactory *self)
{
    return reinterpret_cast<cGZPersistResourceFactory *>(self);
}

// 0xbd4030 / 0xbd40e0: an effects resource (by its key type) reads / writes itself (slot 7 / 6 of its +0 face) on the
// record's stream; false for another type or when the record has no stream. The stream is released afterwards.
bool Transfer(bool reading, cIGZPersistResource *resource, cIGZUnknown *record)
{
    if (resource->GetType() != cTSEffectsResource::kResourceType) return false;
    void *stream = nullptr;
    record->QueryInterface(reading ? cTSEffectsResourceFactory::kIStreamId : cTSEffectsResourceFactory::kOStreamId, &stream);
    bool result = false;
    if (stream) {
        cITSEffectsResource *effects = static_cast<cTSEffectsResource *>(static_cast<cGZPersistResource *>(resource));
        result = reading ? effects->Read(stream) : effects->Write(stream);
    }
    if (stream) static_cast<cIGZUnknown *>(stream)->Release();
    return result;
}

} // namespace

// 0xbd2c50: the base part (count 0, the lock), then our vtable.
cTSEffectsResourceFactory::cTSEffectsResourceFactory() : mRefCount(0)
{
    mLock.Construct();
}

// 0xbd2e30: the base part's destructor (the lock); the scalar deleting destructor (slot 7, 0xbd2e80) is the compiler's.
cTSEffectsResourceFactory::~cTSEffectsResourceFactory()
{
    mLock.Destruct();
}

// 0xbd2ff0
cTSEffectsResourceFactory *cTSEffectsResourceFactory::Create()
{
    return new (std::nothrow) cTSEffectsResourceFactory;
}

bool cTSEffectsResourceFactory::QueryInterface(uint32_t riid, void **out)
{
    return Base(this)->QueryInterface(riid, out);
}

uint32_t cTSEffectsResourceFactory::AddRef()
{
    return Base(this)->AddRef();
}

uint32_t cTSEffectsResourceFactory::Release()
{
    return Base(this)->Release();
}

bool cTSEffectsResourceFactory::CreateInstance(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out, uint32_t a4,
                                               uint32_t a5)
{
    return Base(this)->CreateInstance(record, riid, out, a4, a5);
}

bool cTSEffectsResourceFactory::CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t a4, uint32_t a5)
{
    return Base(this)->CreateInstance2(type, riid, out, a4, a5);
}

bool cTSEffectsResourceFactory::Read(cIGZUnknown *resource, cIGZUnknown *record)
{
    return Base(this)->Read(resource, record);
}

bool cTSEffectsResourceFactory::Write(cIGZUnknown *resource, cIGZUnknown *record)
{
    return Base(this)->Write(resource, record);
}

// slot 8 (the shared body 0x1f62f0)
bool cTSEffectsResourceFactory::CreateInstance3(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out)
{
    return CreateInstance(record, riid, out, 0, 0);
}

// 0xbd2ed0: a new effects resource for the effects type, counted while it is asked for `riid` (its +0 face's slots 1, 0,
// 2); for another type, or when the allocation fails, a null out and false.
bool cTSEffectsResourceFactory::CreateResource(uint32_t type, uint32_t riid, void **out)
{
    if (type == cTSEffectsResource::kResourceType) {
        cITSEffectsResource *resource = cTSEffectsResource::Create();
        if (resource) {
            resource->AddRef();
            const bool result = resource->QueryInterface(riid, out);
            resource->Release();
            return result;
        }
    }
    *out = nullptr;
    return false;
}

// 0xbd4030
bool cTSEffectsResourceFactory::ReadRecord(cIGZPersistResource *resource, cIGZUnknown *record)
{
    return Transfer(true, resource, record);
}

// 0xbd40e0
bool cTSEffectsResourceFactory::WriteRecord(cIGZPersistResource *resource, cIGZUnknown *record)
{
    return Transfer(false, resource, record);
}

// ---- the RVA hooks' adapters ----

using FactoryAdapters = cTSEffectsResourceFactoryAdapters;
using Factory = cTSEffectsResourceFactory;

Factory *__fastcall FactoryAdapters::Construct(void *memory, void *)
{
    return ::new (memory) Factory;
}

void __fastcall FactoryAdapters::Destruct(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Factory::~Factory();
}

Factory *__fastcall FactoryAdapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Factory::~Factory();
    if (flags & 1) ::operator delete(self, sizeof(Factory));
    return self;
}

bool __fastcall FactoryAdapters::CreateResource(Self *self, void *, uint32_t type, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Factory::CreateResource(type, riid, out);
}

bool __fastcall FactoryAdapters::ReadRecord(Self *self, void *, cIGZPersistResource *resource, cIGZUnknown *record)
{
    [[msvc::forceinline_calls]] return self->Factory::ReadRecord(resource, record);
}

bool __fastcall FactoryAdapters::WriteRecord(Self *self, void *, cIGZPersistResource *resource, cIGZUnknown *record)
{
    [[msvc::forceinline_calls]] return self->Factory::WriteRecord(resource, record);
}
