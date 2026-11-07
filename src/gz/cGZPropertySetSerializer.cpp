// The string-keyed property-set serializers: the dispatcher (cGZPropertySetSerializerT, embedded in
// cGZPropertySetResourceString at +0x40) and its binary delegate (cGZPropertySetSerializerBinaryT). /O2 code in the
// 0x551000-0x557400 module. Record format: src/gz/cGZPropertySetSerializer.h. Lane DU (PS3); the delegate's bodies are the
// template shared with the uint32 instantiation (src/gz/cGZPropertySetSerializerDelegates.h, lane EU).
#include "gz/cGZPropertySetSerializer.h"
#include "gz/cGZPropertySetSerializerDelegates.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZAny.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZPersistDBRecord.h"
#include "ifaces/cIGZPropertySetSerializer.h"
#include "ifaces/cIGZPropertySetString.h"
#include "rz/cRZString.h"

#include <cstring>
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

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Dispatcher

// slot 0 (folded 0x555400, one body with BinaryT's): AddRef through slot 1.
bool cGZPropertySetSerializerString::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != gzpropser::kIid) return false;
    *out = static_cast<cIGZPropertySetSerializerString *>(this);
    AddRef();
    return true;
}

// slot 1 (folded 0x15e520)
uint32_t cGZPropertySetSerializerString::AddRef()
{
    return ++mRefCount;
}

// slot 2 (folded 0x242810): the count goes down first; at 0 or below (signed) the deleting destructor (slot 5 with 1).
uint32_t cGZPropertySetSerializerString::Release()
{
    const int32_t count = static_cast<int32_t>(--mRefCount);
    if (count > 0) return static_cast<uint32_t>(count);
    delete this;
    return 0;
}

// Peeks the first dword through the record face (QI 0xe56b8f03): either magic picks the binary reader, anything else
// the XML one; a stream without the record face always goes to XML. A failed peek or rewind returns 0.
bool cGZPropertySetSerializerString::Read(void *stream, cGZPropertySetString *set)
{
    ReleaseOnExit record;
    if (!As<cIGZUnknown>(stream)->QueryInterface(gzpropser::kSeekableStreamIid, &record.object))
        return ReadWith(stream, set, false);
    uint32_t magic;
    if (!As<ifaces::cIGZIStream>(stream)->GetUint32(&magic)) return false;
    if (!As<ifaces::cIGZPersistDBRecord>(record.object)->SeekAbsolute(0)) return false;
    return ReadWith(stream, set, magic == gzpropser::kOtherMagic || magic == gzpropser::kBinaryMagic);
}

// The delegate for the format, created on first use (counted: one reference held here). Both classes are in the standalone
// shape: these are their creation sites (the original builds both inline).
static void *Delegate(void **slot, bool binary)
{
    if (*slot == nullptr) {
        cIGZPropertySetSerializerString *made;
        if (binary)
            made = new cGZPropertySetSerializerBinaryString;
        else
            made = new cGZPropertySetSerializerXmlString;
        made->AddRef();
        *slot = made;
    }
    return *slot;
}

bool cGZPropertySetSerializerString::ReadWith(void *stream, cGZPropertySetString *set, uint8_t binary)
{
    return Serializer(Delegate(binary != 0 ? &mBinary : &mXml, binary != 0))->Read(stream, set);
}

// The delegates ignore the flag: the original passes 0 on (it tail-jumps with the argument slot cleared).
bool cGZPropertySetSerializerString::Write(void *stream, cGZPropertySetString *set, uint8_t binary)
{
    return Serializer(Delegate(binary != 0 ? &mBinary : &mXml, binary != 0))->Write(stream, set, false);
}

// 0x552190 is the compiler's scalar deleting destructor around this one (the vtable store, the two releases, then
// operator delete(this, 0x10) on bit 0).
cGZPropertySetSerializerString::~cGZPropertySetSerializerT()
{
    if (mBinary) Serializer(mBinary)->Release();
    if (mXml) Serializer(mXml)->Release();
}

// The binary delegate: one template with the uint32 one (cGZPropertySetSerializerDelegates.h), this the string instantiation.
template class cGZPropertySetSerializerBinaryT<cKeyStringTraits>;
template struct cGZPropertySetSerializerBinaryAdapters<cKeyStringTraits>;

// ---- the dispatcher's RVA hooks' adapters (qualified calls, bodies inlined; see the header) ----

using DispatcherAdapters = cGZPropertySetSerializerStringAdapters;

bool __fastcall DispatcherAdapters::ReadWith(Self *self, void *, void *stream, cGZPropertySetString *set, uint8_t binary)
{
    [[msvc::forceinline_calls]] return self->Self::ReadWith(stream, set, binary);
}
