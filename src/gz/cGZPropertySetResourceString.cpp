// cGZPropertySetResourceString: a string-keyed property set as a persist resource (/O2, module 0x551000-0x557400). Lane DU
// (PS3); standalone shape lane EL (its set base is cGZPropertySetString itself since lane EQ). The uint32 resource and the
// factory are /O1: cGZPropertySetResource.size.cpp.
#include "gz/cGZPropertySetResource.h"

#include "gz/cGZPropertySet.h"

#include <new>

// ---------------------------------------------------------------------------------------------------------------
// cGZPropertySetResourceString

// 0x5520e0 (and the thunks 0x5520c3 / 0x5520cb) is the compiler's scalar deleting destructor around this one: the bases in
// reverse order (the dispatcher releases its delegates, binary first; the set; cGZPersistResource), then operator delete(this,
// 0x50) on bit 0.
cGZPropertySetResourceString::~cGZPropertySetResourceT() = default;

// 0x5553b0: the set face (0x4bb0d269) and the dispatcher (0xebe701f3), counted on the primary; anything else is
// cGZPersistResource's QueryInterface (the original jumps to its folded body 0x437e30).
bool cGZPropertySetResourceString::QueryInterface(uint32_t riid, void **out)
{
    void *face;
    if (riid == cGZPropertySetString::kIid)
        face = static_cast<cGZPropertySetString *>(this);
    else if (riid == gzpropser::kIid)
        face = static_cast<cGZPropertySetSerializerString *>(this);
    else
        return cGZPersistResource::QueryInterface(riid, out);
    *out = face;
    AddRef();
    return true;
}

// slot 1 (folded 0x5522b0: jmp 0x99ba50)
uint32_t cGZPropertySetResourceString::AddRef()
{
    return cGZPersistResource::AddRef();
}

// slot 2 (folded 0x556090: jmp 0x9b6260)
uint32_t cGZPropertySetResourceString::Release()
{
    return cGZPersistResource::Release();
}

// ---- the RVA hooks' adapters (qualified calls, bodies inlined; see the header) ----

using ResourceAdapters = cGZPropertySetResourceStringAdapters;

// 0x5519c0
ResourceAdapters::Self *__fastcall ResourceAdapters::Construct(void *memory, void *)
{
    return ::new (memory) Self;
}
