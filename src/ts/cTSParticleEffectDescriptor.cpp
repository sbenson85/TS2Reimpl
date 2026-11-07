#include "ts/cTSParticleEffectDescriptor.h"

#include "game/vtable.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZSerializable.h"
#include "misc/UtilitiesC.h"

#include <new>

// 0x4db0c0's body
cTSParticleEffectDescriptor::cTSParticleEffectDescriptor()
    : mRefCount(0), mId(0xffffffffu), mInstance(0xffffffffu), mFlag(false)
{
    mName.Construct();
}

// 0x4dade0
cTSParticleEffectDescriptor::~cTSParticleEffectDescriptor() { mName.Destruct(); }

cTSParticleEffectDescriptor *__cdecl cTSParticleEffectDescriptor::Create()
{
    return new (std::nothrow) cTSParticleEffectDescriptor;
}

bool cTSParticleEffectDescriptor::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kSerializableIid && riid != kClass) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cTSParticleEffectDescriptor::AddRef() { return ++mRefCount; }

uint32_t cTSParticleEffectDescriptor::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

bool cTSParticleEffectDescriptor::Write(ifaces::cIGZOStream *stream)
{
    auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
    if (metadata) metadata = metadata->Construct(kWriteVersion);
    stream->SetMetadata(metadata);
    stream->SetUint32(mId);
    stream->SetUint32(mInstance);
    stream->SetUint32(mFlag);
    stream->SetGZStr(&mName);
    return stream->GetError() == 0;
}

// The version the metadata carries is read and dropped. The flag's word starts as whatever the stack word held, which in
// the original is the stream argument itself, so a read that fails leaves the flag set.
bool cTSParticleEffectDescriptor::Read(ifaces::cIGZIStream *stream)
{
    RawSlot<78, const uint32_t *>(RawSlot<20, void *>(stream));
    stream->GetUint32(&mId);
    stream->GetUint32(&mInstance);
    uint32_t flag = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stream)); // ORIGINAL-BUG: unread flag reads as true
    stream->GetUint32(&flag);
    mFlag = flag != 0;
    stream->GetGZStr(&mName);
    return stream->GetError() == 0;
}

void cTSParticleEffectDescriptor::SetName(const cRZString *name) { mName.mString = name->mString; }
