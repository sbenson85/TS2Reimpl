#include "rz/cRZStringResource.h"

#include <new>

bool cRZStringResource::QueryInterface(uint32_t riid, void **out)
{
    if (riid == cGZPersistResource::kIid) {
        // The resource face (the original's null test of this, a static_cast's, never fires: a slot call needs an
        // object); the reference through the string face.
        *out = &mResource;
        mString.AsInterface()->AddRef();
        return true;
    }
    return mString.QueryInterface(riid, out);
}

uint32_t cRZStringResource::AddRef() { return mResource.cGZPersistResource::AddRef(); }

uint32_t cRZStringResource::Release() { return mResource.cGZPersistResource::Release(); }

cRZStringResource *cRZStringResource::DeletingDestructor(uint32_t flags)
{
    // The resource part first (it stores cGZPersistResource's vtable), then the string (cRZString's vtable, the text
    // freed and left empty).
    cGZPersistResourceAdapters::Destruct(&mResource, nullptr);
    mString.Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

bool __fastcall cRZStringResourceThunks::QueryInterface(void *face, void *, uint32_t riid, void **out)
{
    return cRZStringResource::FromResource(face)->QueryInterface(riid, out);
}

uint32_t __fastcall cRZStringResourceThunks::AddRef(void *face, void *)
{
    return cRZStringResource::FromResource(face)->AddRef();
}

uint32_t __fastcall cRZStringResourceThunks::Release(void *face, void *)
{
    return cRZStringResource::FromResource(face)->Release();
}

cRZStringResource *__fastcall cRZStringResourceThunks::DeletingDestructor(void *face, void *, uint32_t flags)
{
    return cRZStringResource::FromResource(face)->DeletingDestructor(flags);
}
