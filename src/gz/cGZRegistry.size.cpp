#include "gz/cGZRegistry.h"


#include <new>

// cGZRegistryEntry --------------------------------------------------------------------------

void cGZRegistryEntry::Destruct()
{
    // The second value, the first value, then the key, each a cRZString: vtable reset and the
    // string released.
    mSecond.Destruct();
    mFirst.Destruct();
    mKey.Destruct();
}

// The tree is the standard multimap (an empty one: its head node linked to itself) over the same raw words.
cGZRegistry::cGZRegistry() : rz::cRZSystemService(kServiceId, kPriority) { ::new (&mTree) cGZRegistryMap(); }

cGZRegistry::~cGZRegistry() { Map().~cGZRegistryMap(); }

bool cGZRegistry::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != kInterfaceId) return rz::cRZSystemService::QueryInterface(riid, ppv);
    *ppv = static_cast<cIGZRegistry *>(this);
    AddRef();
    return true;
}

uint32_t cGZRegistry::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cGZRegistry::Release() { return rz::cRZSystemService::Release(); }

// ---- adapters --------------------------------------------------------------------------------------------------------

cGZRegistry *__fastcall cGZRegistryAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZRegistry;
}
