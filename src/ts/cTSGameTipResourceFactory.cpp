// cTSGameTipResourceFactory (lane KX). Layout and slots: src/ts/cTSGameTipResourceFactory.h. Slots 0-6 are the base factory's
// members (ours, old shape, the same layout) run on this object.
#include "ts/cTSGameTipResourceFactory.h"

#include "gz/cGZPersistResourceFactory.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ts/cTSFamilyRelationshipArray.h" // cRZAutoRefCount
#include "ts/cTSGameTip.h"

#include <new>

namespace {

cGZPersistResourceFactory *Base(cTSGameTipResourceFactory *self)
{
    return reinterpret_cast<cGZPersistResourceFactory *>(self);
}

// 0xcb6c20 / 0xcb6cf0: a game tip (by its key type) reads / writes itself (cIGZSerializable slot 4 / 3) on the record's
// stream; false for another type, or when the resource has no cIGZSerializable or the record no stream. Both are asked
// whatever the first answered, and released afterwards (the stream first).
bool Transfer(bool reading, cIGZPersistResource *resource, cIGZUnknown *record)
{
    if (resource->GetType() != cTSGameTip::kType) return false;
    cRZAutoRefCount<cIGZSerializable> serializable;
    resource->QueryInterface(cTSGameTip::kSerializableIid, serializable.AsPPVoid());
    cRZAutoRefCount<cIGZUnknown> stream;
    record->QueryInterface(reading ? cTSGameTipResourceFactory::kIStreamId : cTSGameTipResourceFactory::kOStreamId,
                           stream.AsPPVoid());
    bool result = false;
    if (serializable && stream) {
        result = reading ? serializable->Read(reinterpret_cast<ifaces::cIGZIStream *>(stream.Get()))
                         : serializable->Write(reinterpret_cast<ifaces::cIGZOStream *>(stream.Get()));
    }
    return result;
}

} // namespace

// Inline in cTSGameTipService::Init (0xcb6170): the base part (0x9b6010: count 0, the lock), then our vtable.
cTSGameTipResourceFactory::cTSGameTipResourceFactory() : mRefCount(0)
{
    mLock.Construct();
}

// The base part's destructor (0x9b6040: the lock); the scalar deleting destructor (slot 7, folded 0x9b3010) is the
// compiler's.
cTSGameTipResourceFactory::~cTSGameTipResourceFactory()
{
    mLock.Destruct();
}

bool cTSGameTipResourceFactory::QueryInterface(uint32_t riid, void **out) { return Base(this)->QueryInterface(riid, out); }
uint32_t cTSGameTipResourceFactory::AddRef() { return Base(this)->AddRef(); }
uint32_t cTSGameTipResourceFactory::Release() { return Base(this)->Release(); }

bool cTSGameTipResourceFactory::CreateInstance(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out, uint32_t a4,
                                               uint32_t a5)
{
    return Base(this)->CreateInstance(record, riid, out, a4, a5);
}

bool cTSGameTipResourceFactory::CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t a4, uint32_t a5)
{
    return Base(this)->CreateInstance2(type, riid, out, a4, a5);
}

bool cTSGameTipResourceFactory::Read(cIGZUnknown *resource, cIGZUnknown *record) { return Base(this)->Read(resource, record); }
bool cTSGameTipResourceFactory::Write(cIGZUnknown *resource, cIGZUnknown *record) { return Base(this)->Write(resource, record); }

// slot 8 (the shared body 0x1f62f0)
bool cTSGameTipResourceFactory::CreateInstance3(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out)
{
    return CreateInstance(record, riid, out, 0, 0);
}

// 0xcb5bb0: a new game tip for the tip type, counted while it is asked for `riid` (its +0 face's slots 1, 0, 2); for
// another type a null out and false.
bool cTSGameTipResourceFactory::CreateResource(uint32_t type, uint32_t riid, void **out)
{
    if (type != cTSGameTip::kType) {
        *out = nullptr;
        return false;
    }
    // ORIGINAL-BUG: when the allocation fails the AddRef is skipped but QueryInterface is still called on the null tip.
    cIGZPersistResource *tip = new (std::nothrow) cTSGameTip;
    if (tip) tip->AddRef();
    const bool result = tip->QueryInterface(riid, out);
    if (tip) tip->Release();
    return result;
}

// 0xcb6c20
bool cTSGameTipResourceFactory::ReadRecord(cIGZPersistResource *resource, cIGZUnknown *record)
{
    return Transfer(true, resource, record);
}

// 0xcb6cf0
bool cTSGameTipResourceFactory::WriteRecord(cIGZPersistResource *resource, cIGZUnknown *record)
{
    return Transfer(false, resource, record);
}
