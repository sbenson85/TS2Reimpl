#include "ts/cTSPersistResourceFactoryFamilyRelationshipArray.h"

#include "ifaces/cIGZPersistDBRecord.h"
#include "ts/cTSFamilyRelationshipArray.h" // the resource, cRZAutoRefCount

#include <new>

// The scalar deleting destructor (slot 7, 0x4f2170) is the compiler's: this destructor (the vtable store), then
// operator delete(this, 8) when bit 0 of the flags is set.
cTSPersistResourceFactoryFamilyRelationshipArray::~cTSPersistResourceFactoryFamilyRelationshipArray() = default;

// 0x4f2740
cTSPersistResourceFactoryFamilyRelationshipArray *cTSPersistResourceFactoryFamilyRelationshipArray::Create()
{
    return new cTSPersistResourceFactoryFamilyRelationshipArray;
}

// slot 0 (folded 0x1ee250)
bool cTSPersistResourceFactoryFamilyRelationshipArray::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kFactoryId) return false;
    *out = this;
    AddRef();
    return true;
}

// slot 1 (folded 0x148fb0)
uint32_t cTSPersistResourceFactoryFamilyRelationshipArray::AddRef()
{
    return ++mRefCount;
}

// slot 2 (folded 0x1ee310)
uint32_t cTSPersistResourceFactoryFamilyRelationshipArray::Release()
{
    if (mRefCount <= 1) {
        delete this; // slot 7 with 1
        return 0;
    }
    return --mRefCount;
}

// slot 3 (folded 0x431e60): a resource of the record's key type, asked for the persist resource face, then for `riid` into
// `out`, then read from the record (slot 5). The created resource is released at the end whatever happened.
bool cTSPersistResourceFactoryFamilyRelationshipArray::CreateInstance(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out,
                                                                      uint32_t, uint32_t)
{
    const uint32_t type = *static_cast<const uint32_t *>(record->GetKey()); // the key's type word
    cRZAutoRefCount<cIGZUnknown> resource;
    bool result = false;
    if (CreateInstanceByType(type, kResourceId, resource.AsPPVoid(), 0, 0) && resource->QueryInterface(riid, out) &&
        Read(resource.Get(), reinterpret_cast<cIGZUnknown *>(record)))
        result = true;
    return result;
}

// 0x4f25d0: a new family relationship array for the FAMT type, asked for `riid`; false for any other type.
bool cTSPersistResourceFactoryFamilyRelationshipArray::CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t,
                                                                            uint32_t)
{
    if (type != cTSFamilyRelationshipArray::kClassId) return false;
    // ORIGINAL-BUG: when the allocation fails the AddRef is skipped but QueryInterface is still called on the null object.
    const cRZAutoRefCount<cTSFamilyRelationshipArray> array(new cTSFamilyRelationshipArray);
    return array->QueryInterface(riid, out);
}

// 0x4f3280: the resource's family relationship array reads itself from the record (slot 13, ReadFrom).
bool cTSPersistResourceFactoryFamilyRelationshipArray::Read(cIGZUnknown *resource, cIGZUnknown *record)
{
    cRZAutoRefCount<cITSFamilyRelationshipArray> array;
    bool result = false; // ReadFrom's byte goes out as it is (not normalised to 0 / 1), as the original's `mov bl, al`
    if (resource->QueryInterface(cTSFamilyRelationshipArray::kInterfaceId, array.AsPPVoid())) result = array->ReadFrom(record);
    return result;
}

// 0x4f3f70: the same with slot 14 (WriteTo).
bool cTSPersistResourceFactoryFamilyRelationshipArray::Write(cIGZUnknown *resource, cIGZUnknown *record)
{
    cRZAutoRefCount<cITSFamilyRelationshipArray> array;
    bool result = false;
    if (resource->QueryInterface(cTSFamilyRelationshipArray::kInterfaceId, array.AsPPVoid())) result = array->WriteTo(record);
    return result;
}

// slot 8 (folded 0x147a30)
uint32_t cTSPersistResourceFactoryFamilyRelationshipArray::GetRefCount()
{
    return mRefCount;
}

// ---- the RVA hooks' adapters (qualified calls, bodies inlined; see the header) ----

using Adapters = cTSPersistResourceFactoryFamilyRelationshipArrayAdapters;

// The deleting destructor has no name to call without dispatch: its two steps by hand.
Adapters::Self *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}
