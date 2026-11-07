// The /O1 half of the property-set resources (factory module 0xc2bb00-0xc2c4d0): the uint32-keyed resource and
// cGZPropertySetFactory, the persist factory of the four property-set record types. Lane DU (PS3).
#include "gz/cGZPropertySetResource.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZPersistDBRecord.h"
#include "ifaces/cIGZPropertySetSerializer.h"
#include "rz/RZFrameworkHelpers.h"

#include <new>

namespace {
// A counted pointer the original holds in a local and releases on the way out.
struct Held {
    void *object = nullptr;
    ~Held()
    {
        if (object) As<cIGZUnknown>(object)->Release();
    }
};

enum class KeyKind { None, String, Uint32 };
KeyKind KindOf(uint32_t type)
{
    if (type == cGZPropertySetFactory::kTypeStringXml || type == cGZPropertySetFactory::kTypeStringBinary) return KeyKind::String;
    if (type == cGZPropertySetFactory::kTypeUint32Xml || type == cGZPropertySetFactory::kTypeUint32Binary) return KeyKind::Uint32;
    return KeyKind::None;
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// cGZPropertySetResourceUint32

// 0xc2bcb3 (slot 12 0xc2bcf0 is the compiler's scalar deleting destructor around it, delete 0x44): the dispatcher (its
// delegates released, binary first), the set (Clear, the entries), cGZPersistResource.
cGZPropertySetResourceUint32::~cGZPropertySetResourceT() = default;

// 0xc2c126: the set face (0xebb10cdd) and the dispatcher, counted on the primary; anything else is cGZPersistResource's
// QueryInterface (the original jumps to its folded body 0x437e30).
bool cGZPropertySetResourceUint32::QueryInterface(uint32_t riid, void **out)
{
    void *face;
    if (riid == cGZPropertySetUint32::kIid)
        face = static_cast<cGZPropertySetUint32 *>(this);
    else if (riid == gzpropser::kIid)
        face = static_cast<cGZPropertySetSerializerUint32 *>(this);
    else
        return cGZPersistResource::QueryInterface(riid, out);
    *out = face;
    AddRef();
    return true;
}

// slot 1 (folded 0x5522b0: jmp 0x99ba50)
uint32_t cGZPropertySetResourceUint32::AddRef()
{
    return cGZPersistResource::AddRef();
}

// slot 2 (folded 0x556090: jmp 0x9b6260)
uint32_t cGZPropertySetResourceUint32::Release()
{
    return cGZPersistResource::Release();
}

// ---- the uint32 resource's RVA hooks' adapters (qualified calls, bodies inlined) ----

using U32ResourceAdapters = cGZPropertySetResourceUint32Adapters;

// 0xc2bb25
U32ResourceAdapters::Self *__fastcall U32ResourceAdapters::Construct(void *memory, void *)
{
    return ::new (memory) Self;
}

// 0xc2bcb3
void __fastcall U32ResourceAdapters::Destruct(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
}

// ---------------------------------------------------------------------------------------------------------------
// cGZPropertySetFactory

// +4 slot 3 (0xc0edaa through the thunk 0xc0ed0e): the compiler's scalar deleting destructor around this one (cRZUnknown's
// destructor, then operator delete(this, 0xc) on bit 0).
cGZPropertySetFactory::~cGZPropertySetFactory() = default;

// 0xc2be41 (the constructor 0xc2bb7a inline: the interface's vtable, the cRZUnknown part with count 0, the class's vtables)
cGZPropertySetFactory *cGZPropertySetFactory::Create()
{
    return new cGZPropertySetFactory;
}

// slot 0 (folded 0xc14428): this for the factory interface (counted through slot 1); anything else is cRZUnknown's (interface
// id 1 gives the +4 face).
bool cGZPropertySetFactory::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kFactoryIid) return rz::cRZUnknown::QueryInterface(riid, out);
    *out = static_cast<cIGZPersistResourceFactory *>(this);
    AddRef();
    return true;
}

// slot 1 (folded 0x1dab20)
uint32_t cGZPropertySetFactory::AddRef()
{
    return rz::cRZUnknown::AddRef();
}

// slot 2 (folded 0x1e3970): at 1 or below the deleting destructor (+4 slot 3)
uint32_t cGZPropertySetFactory::Release()
{
    return rz::cRZUnknown::Release();
}

// slot 3, 0xc2bd36: a resource of the record's type (slot 4), then read from the record (slot 5).
bool cGZPropertySetFactory::CreateInstance(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out, uint32_t a4, uint32_t a5)
{
    const uint32_t type = *static_cast<const uint32_t *>(record->GetKey());
    if (!CreateInstanceByType(type, riid, out, a4, a5)) return false;
    return Read(static_cast<cIGZUnknown *>(*out), reinterpret_cast<cIGZUnknown *>(record)) != 0;
}

// slot 4, 0xc2bd7c: a new resource for the type, typed (SetType) and asked for riid; the factory's own reference goes.
bool cGZPropertySetFactory::CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t, uint32_t)
{
    Held resource;
    switch (KindOf(type)) {
    case KeyKind::String:
        resource.object = static_cast<cGZPersistResource *>(new (std::nothrow) cGZPropertySetResourceString);
        break;
    case KeyKind::Uint32:
        resource.object = static_cast<cGZPersistResource *>(new (std::nothrow) cGZPropertySetResourceUint32);
        break;
    case KeyKind::None:
        break;
    }
    if (resource.object) As<cIGZUnknown>(resource.object)->AddRef();
    // ORIGINAL-BUG: an unknown type leaves no resource, and SetType is called on a null pointer (crash).
    VirtualCall<void(__thiscall *)(void *, uint32_t), 8>(resource.object, type);
    return As<cIGZUnknown>(resource.object)->QueryInterface(riid, out);
}

// slot 5, 0xc2c173: the record's input stream, the resource's set and serializer faces (by the record's key type),
// then the serializer reads. 0 when a face is missing or the type is not one of the four.
bool cGZPropertySetFactory::Read(cIGZUnknown *resource, cIGZUnknown *record)
{
    Held stream;
    if (!record->QueryInterface(kIStreamIid, &stream.object)) return false;
    const uint32_t type = *static_cast<const uint32_t *>(As<ifaces::cIGZPersistDBRecord>(record)->GetKey());
    const KeyKind kind = KindOf(type);
    if (kind == KeyKind::None) return false;
    bool result = false;
    Held set;
    if (resource->QueryInterface(kind == KeyKind::String ? cGZPropertySetString::kIid : cGZPropertySetUint32::kIid, &set.object)) {
        Held serializer;
        if (resource->QueryInterface(gzpropser::kIid, &serializer.object))
            result = As<ifaces::cIGZPropertySetSerializer>(serializer.object)->Read(stream.object, set.object);
    }
    return result;
}

// slot 6, 0xc2c2be: as Read, with the output stream; 0xebfee342 and 0xebfee349 are written binary, the other two XML.
bool cGZPropertySetFactory::Write(cIGZUnknown *resource, cIGZUnknown *record)
{
    Held stream;
    if (!record->QueryInterface(kOStreamIid, &stream.object)) return false;
    const uint32_t type = *static_cast<const uint32_t *>(As<ifaces::cIGZPersistDBRecord>(record)->GetKey());
    const KeyKind kind = KindOf(type);
    if (kind == KeyKind::None) return false;
    // The flag goes out as a whole dword 0 / 1 (the original's argument; a bool argument may carry stale upper bytes).
    const uint32_t binary = type == kTypeStringBinary || type == kTypeUint32Binary;
    bool result = false;
    Held set;
    if (resource->QueryInterface(kind == KeyKind::String ? cGZPropertySetString::kIid : cGZPropertySetUint32::kIid, &set.object)) {
        Held serializer;
        if (resource->QueryInterface(gzpropser::kIid, &serializer.object))
            result = VirtualCall<bool(__thiscall *)(void *, void *, void *, uint32_t), 4>(serializer.object, stream.object, set.object, binary);
    }
    return result;
}

// ---- the factory's RVA hooks' adapters (qualified calls, bodies inlined; see the header) ----

using FactoryAdapters = cGZPropertySetFactoryAdapters;

// 0xc2bb7a
FactoryAdapters::Self *__fastcall FactoryAdapters::Construct(void *memory, void *)
{
    return ::new (memory) Self;
}
