#include "ts/cTSRelationshipEntry.h"

#include "game/game.h"
#include "game/vtable.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZPersistDBRecord.h"

#include <new>

namespace {
// A counted stream taken from a record: released when the scope ends, also on an exception (the original's EH frame).
struct StreamHold {
    void *stream = nullptr;
    ~StreamHold() noexcept(false)
    {
        if (stream) As<cIGZUnknown>(stream)->Release();
    }
};
} // namespace

// ---- construction and destruction ----

// Inline in 0x55ac60 and 0x55b420: the compiler installs our three vtables.
cTSRelationshipEntry::cTSRelationshipEntry(const cGZPersistResourceKey &key) : mRefCount(0), mKey(key), mValues() {}

// ---- cIGZUnknown ----

// 0x55b2c0: 1 and cIGZPersistResource both hand out the persist face (null for a null entry).
bool cTSRelationshipEntry::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case 1:
    case kPersistResourceId:
        break;
    default:
        return false;
    }
    *out = static_cast<cIGZPersistResource *>(this);
    AddRef();
    return true;
}

// slot 1 (folded 0x15e520)
uint32_t cTSRelationshipEntry::AddRef()
{
    return ++mRefCount;
}

// 0x55ba70: destroyed inline (the vector's block, then the 0x2c bytes) at a count of 1 or below; the vtables are not rewritten.
uint32_t cTSRelationshipEntry::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

// ---- cITSRelationshipEntry ----

// slot 3 (folded 0x1c04e0)
cIGZPersistResource *cTSRelationshipEntry::GetPersistResource()
{
    return static_cast<cIGZPersistResource *>(this);
}

// 0x55bd20: the stored values are cleared first, whatever the stream holds. Version other than 2: false. A short stream
// leaves whatever the stream wrote into the locals (it never checks the stream's results).
bool cTSRelationshipEntry::ReadFromRecord(void *record)
{
    StreamHold hold;
    bool ok = false;
    if (As<ifaces::cIGZPersistDBRecord>(record)->QueryInterface(kStreamReaderId, &hold.stream)) {
        mValues.clear();
        auto *stream = As<ifaces::cIGZIStream>(hold.stream);
        uint32_t version;
        stream->GetUint32(&version);
        if (version == kRecordVersion) {
            uint32_t count;
            stream->GetUint32(&count);
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t value;
                stream->GetUint32(&value);
                mValues.push_back(value);
            }
            ok = true;
        }
    }
    return ok;
}

// 0x55be00
bool cTSRelationshipEntry::WriteToRecord(void *record)
{
    StreamHold hold;
    bool ok = false;
    if (As<ifaces::cIGZPersistDBRecord>(record)->QueryInterface(kStreamWriterId, &hold.stream)) {
        auto *stream = As<ifaces::cIGZOStream>(hold.stream);
        stream->SetUint32(kRecordVersion);
        stream->SetUint32(static_cast<uint32_t>(mValues.size()));
        for (uint32_t i = 0; i < mValues.size(); ++i) stream->SetUint32(mValues[i]);
        ok = true;
    }
    return ok;
}

// slot 6 (folded 0x4e9710)
uint32_t cTSRelationshipEntry::GetValueCount()
{
    return static_cast<uint32_t>(mValues.size());
}

// 0x55bf10
bool cTSRelationshipEntry::SetValueCount(uint32_t count)
{
    mValues.resize(count);
    return true;
}

// 0x55aed0
uint32_t cTSRelationshipEntry::GetValue(uint32_t index)
{
    const uint32_t *values = mValues.data();
    if (index < mValues.size()) return values[index];
    return 0;
}

// 0x55bee0
bool cTSRelationshipEntry::SetValue(uint32_t index, uint32_t value)
{
    if (index < mValues.size()) {
        mValues[index] = value;
        return true;
    }
    return false;
}

// ---- cIGZPersistResource face (the key; all folded) ----

cGZPersistResourceKey cTSRelationshipEntry::GetKey()
{
    return mKey;
}

bool cTSRelationshipEntry::SetKey(const cGZPersistResourceKey &key)
{
    mKey = key;
    return true;
}

uint32_t cTSRelationshipEntry::GetType()
{
    return mKey.type;
}

uint32_t cTSRelationshipEntry::GetGroup()
{
    return mKey.group;
}

uint32_t cTSRelationshipEntry::GetInstance()
{
    return mKey.instance;
}

bool cTSRelationshipEntry::SetType(uint32_t type)
{
    mKey.type = type;
    return true;
}

bool cTSRelationshipEntry::SetGroup(uint32_t group)
{
    mKey.group = group;
    return true;
}

bool cTSRelationshipEntry::SetInstance(uint32_t instance)
{
    mKey.instance = instance;
    return true;
}

uint32_t cTSRelationshipEntry::GetRefCount()
{
    return mRefCount;
}

// ---- cIGZSerializable face ----

// slot 3 (folded 0x18a7a0)
bool cTSRelationshipEntry::Write(ifaces::cIGZOStream *)
{
    return false;
}

// 0x55b370: the inline format (s32 count, then the values); a negative count is refused after the values are cleared.
bool cTSRelationshipEntry::Read(ifaces::cIGZIStream *stream)
{
    mValues.clear();
    int32_t count;
    stream->GetSint32(&count);
    if (count < 0) return false;
    mValues.resize(static_cast<uint32_t>(count));
    for (int32_t i = 0; i < count; ++i) stream->GetUint32(&mValues.data()[i]);
    return true;
}

// 0x55aff0
uint32_t cTSRelationshipEntry::GetGZCLSID()
{
    return kClassId;
}

// ---- the RVA hooks' adapters ----
// Qualified calls (no virtual dispatch: an entry built with the game's vtables would reach these very hooks again), bodies
// forced inline so each adapter is the function itself (matchcheck compares the adapter). A base-to-derived static_cast finds
// the object from a face.

using Adapters = cTSRelationshipEntryAdapters;

bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}

uint32_t __fastcall Adapters::Release(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Release();
}

uint32_t __fastcall Adapters::GetValue(Self *self, void *, uint32_t index)
{
    [[msvc::forceinline_calls]] return self->Self::GetValue(index);
}

bool __fastcall Adapters::SetValue(Self *self, void *, uint32_t index, uint32_t value)
{
    [[msvc::forceinline_calls]] return self->Self::SetValue(index, value);
}

bool __fastcall Adapters::Read(cIGZSerializable *face, void *, ifaces::cIGZIStream *stream)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::Read(stream);
}

uint32_t __fastcall Adapters::GetGZCLSID(cIGZSerializable *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetGZCLSID();
}
