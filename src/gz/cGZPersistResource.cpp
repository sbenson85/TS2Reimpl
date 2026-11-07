// cGZPersistResource: the persist resources' base (/O2). Lane EL. Layout and slots: src/gz/cGZPersistResource.h.
#include "gz/cGZPersistResource.h"

#include "rz/cRZRefCount.h" // RZAtomicAdd

#include <new>

// 0x9b6030 (the vtable store); slot 12 (0x9b6050) is the compiler's scalar deleting destructor around it (delete 0x1c on bit 0).
cGZPersistResource::~cGZPersistResource() = default;

// slot 0 (folded 0x437e30): this for cIGZUnknown (1) and cIGZPersistResource, counted through slot 1; else false, out untouched.
bool cGZPersistResource::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case 1:
    case kIid:
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

// slot 1 (folded 0x99ba50)
uint32_t cGZPersistResource::AddRef()
{
    return static_cast<uint32_t>(RZAtomicAdd(&mRefCount, 1));
}

// slot 2, 0x9b6260: a nonzero count is returned as is; at zero the object deletes itself (slot 12, flag 1).
uint32_t cGZPersistResource::Release()
{
    const uint32_t count = static_cast<uint32_t>(RZAtomicAdd(&mRefCount, -1));
    if (count != 0) return count;
    delete this;
    return 0;
}

cGZPersistResourceKey cGZPersistResource::GetKey() { return mKey; } // slot 3 (folded 0x18a670)

// slot 4 (folded 0x4f3d80)
bool cGZPersistResource::SetKey(const cGZPersistResourceKey &key)
{
    mKey = key;
    return true;
}

uint32_t cGZPersistResource::GetType() { return mKey.type; }         // slot 5 (folded 0x145770)
uint32_t cGZPersistResource::GetGroup() { return mKey.group; }       // slot 6 (folded 0x149cf0)
uint32_t cGZPersistResource::GetInstance() { return mKey.instance; } // slot 7 (folded 0x149d00)

// slot 8 (folded 0x1eeb70)
bool cGZPersistResource::SetType(uint32_t type)
{
    mKey.type = type;
    return true;
}

// slot 9 (folded 0x1eec40)
bool cGZPersistResource::SetGroup(uint32_t group)
{
    mKey.group = group;
    return true;
}

// slot 10 (folded 0x47d410)
bool cGZPersistResource::SetInstance(uint32_t instance)
{
    mKey.instance = instance;
    return true;
}

uint32_t cGZPersistResource::GetRefCount() { return static_cast<uint32_t>(mRefCount); } // slot 11 (folded 0x147a30)

// ---- the RVA hooks' adapters (qualified calls, bodies inlined; see the header) ----

using PersistResourceAdapters = cGZPersistResourceAdapters;

// 0x9b5fd0
PersistResourceAdapters::Self *__fastcall PersistResourceAdapters::Construct(void *memory, void *)
{
    return ::new (memory) Self;
}

// 0x9b6030
void __fastcall PersistResourceAdapters::Destruct(Self *self, void *)
{
    self->Self::~Self();
}

// 0x9b6050: no name to call without dispatch, so the two steps by hand.
PersistResourceAdapters::Self *__fastcall PersistResourceAdapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}

// 0x9b6260
uint32_t __fastcall PersistResourceAdapters::Release(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Release();
}
