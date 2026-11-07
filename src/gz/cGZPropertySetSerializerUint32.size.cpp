// The uint32-keyed property-set dispatcher (cGZPropertySetSerializerT<cKeyUint32Traits>, the base of cGZPropertySetResourceUint32
// at +0x34). /O1 code in the 0x236be6-0x2429xx module. Constructor / destructor lane DY; the standalone shape and the slots lane EQ.
#include "gz/cGZPropertySetSerializer.h"

#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZPersistDBRecord.h"
#include "ifaces/cIGZPropertySetSerializer.h"

#include <new>

namespace {

ifaces::cIGZPropertySetSerializer *Serializer(void *object) { return As<ifaces::cIGZPropertySetSerializer>(object); }

// A counted pointer the original keeps in a local and releases on the way out (also on failure).
struct ReleaseOnExit {
    void *object = nullptr;
    ~ReleaseOnExit()
    {
        if (object) As<cIGZUnknown>(object)->Release();
    }
};

// The delegate for the format, created on first use (the original's constructors 0x236be6, 0xc bytes / 0x241ea6, 0x5c bytes, on
// operator new's block: our classes since lane EU), stored through RZUnknownSlot::Assign (AddRef the new one; the slot was
// empty).
void *Delegate(void **slot, bool binary)
{
    if (*slot == nullptr) {
        cIGZPropertySetSerializerUint32 *made;
        if (binary)
            made = new cGZPropertySetSerializerBinaryUint32;
        else
            made = new cGZPropertySetSerializerXmlUint32;
        made->AddRef();
        *slot = made;
    }
    return *slot;
}

} // namespace

// 0x241f2c: the delegates are released in reverse order of declaration (binary first); the pointers are left as they were.
cGZPropertySetSerializerUint32::~cGZPropertySetSerializerT()
{
    if (mBinary) Serializer(mBinary)->Release();
    if (mXml) Serializer(mXml)->Release();
}

// slot 0 (folded 0x237d6f): AddRef through slot 1.
bool cGZPropertySetSerializerUint32::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != gzpropser::kIid) return false;
    *out = static_cast<cIGZPropertySetSerializerUint32 *>(this);
    AddRef();
    return true;
}

// slot 1 (folded 0x15e520)
uint32_t cGZPropertySetSerializerUint32::AddRef()
{
    return ++mRefCount;
}

// slot 2 (folded 0x242810): the count goes down first; at 0 or below (signed) the deleting destructor (slot 5 with 1).
uint32_t cGZPropertySetSerializerUint32::Release()
{
    const int32_t count = static_cast<int32_t>(--mRefCount);
    if (count > 0) return static_cast<uint32_t>(count);
    delete this;
    return 0;
}

// 0x2425ad: peeks the first dword through the record face (QI 0xe56b8f03): either magic picks the binary reader, anything else
// the XML one; a stream without the record face always goes to XML. A failed peek or rewind returns 0.
bool cGZPropertySetSerializerUint32::Read(void *stream, cGZPropertySetUint32 *set)
{
    ReleaseOnExit record;
    if (!As<cIGZUnknown>(stream)->QueryInterface(gzpropser::kSeekableStreamIid, &record.object))
        return ReadWith(stream, set, false);
    uint32_t magic;
    if (!As<ifaces::cIGZIStream>(stream)->GetUint32(&magic)) return false;
    if (!As<ifaces::cIGZPersistDBRecord>(record.object)->SeekAbsolute(0)) return false;
    return ReadWith(stream, set, magic == gzpropser::kOtherMagic || magic == gzpropser::kBinaryMagic);
}

// 0x242547
bool cGZPropertySetSerializerUint32::ReadWith(void *stream, cGZPropertySetUint32 *set, uint8_t binary)
{
    return Serializer(Delegate(binary != 0 ? &mBinary : &mXml, binary != 0))->Read(stream, set);
}

// 0x242899: the delegates ignore the flag; the original pushes 0.
bool cGZPropertySetSerializerUint32::Write(void *stream, cGZPropertySetUint32 *set, uint8_t binary)
{
    return Serializer(Delegate(binary != 0 ? &mBinary : &mXml, binary != 0))->Write(stream, set, false);
}

// ---- the RVA hooks' adapters (qualified calls, bodies inlined) ----

using U32DispatcherAdapters = cGZPropertySetSerializerUint32Adapters;

U32DispatcherAdapters::Self *__fastcall U32DispatcherAdapters::Construct(void *memory, void *) { return ::new (memory) Self; }

void __fastcall U32DispatcherAdapters::Destruct(Self *self, void *) { [[msvc::forceinline_calls]] self->Self::~Self(); }

bool __fastcall U32DispatcherAdapters::Read(Self *self, void *, void *stream, cGZPropertySetUint32 *set)
{
    [[msvc::forceinline_calls]] return self->Self::Read(stream, set);
}

bool __fastcall U32DispatcherAdapters::ReadWith(Self *self, void *, void *stream, cGZPropertySetUint32 *set, uint8_t binary)
{
    [[msvc::forceinline_calls]] return self->Self::ReadWith(stream, set, binary);
}

bool __fastcall U32DispatcherAdapters::Write(Self *self, void *, void *stream, cGZPropertySetUint32 *set, uint8_t binary)
{
    [[msvc::forceinline_calls]] return self->Self::Write(stream, set, binary);
}
