#include "world/cWallLayer.h"

#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "rz/cRZString.h"

#include <new>

namespace nTSWorld {
namespace {
// A local cRZString of the original's, destroyed when the scope ends, also when a stream call throws (its EH frame).
struct RecordName {
    cRZString mString;
    RecordName() { mString.Construct(); }
    explicit RecordName(const char *text) { mString.Construct(text); }
    ~RecordName() { mString.Destruct(); }
};

constexpr uint32_t kVersion = 1;
} // namespace

cWallLayer::cWallLayer() {}

cWallLayer::~cWallLayer() = default;

bool cWallLayer::QueryInterface(uint32_t riid, void **out)
{
    if (!out || (riid != 1 && riid != kClassId)) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cWallLayer::AddRef() { return ++mRefCount; }

uint32_t cWallLayer::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

uint32_t cWallLayer::GetGZCLSID() { return kClassId; }

void cWallLayer::Clear() { mEntries.clear(); }

void cWallLayer::Erase(int32_t key) { mEntries.erase(key); }

void cWallLayer::SetHalf(int32_t key, int32_t which, const uint16_t *value)
{
    const auto found = mEntries.find(key);
    if (found == mEntries.end()) return;
    uint32_t &second = found->second.second;
    if (which == 0) second = (second & 0xffffu) | uint32_t(*value) << 16;
    else if (which == 1) second = (second & 0xffff0000u) | *value;
}

bool cWallLayer::Has(int32_t key) { return mEntries.find(key) != mEntries.end(); }

uint16_t cWallLayer::Half(int32_t key, int32_t which)
{
    const auto found = mEntries.find(key);
    if (found == mEntries.end()) return 0;
    if (which == 0) return uint16_t(found->second.second >> 16);
    if (which == 1) return uint16_t(found->second.second);
    return 0;
}

uint32_t cWallLayer::First(int32_t key)
{
    const auto found = mEntries.find(key);
    return found == mEntries.end() ? 0 : found->second.first;
}

bool cWallLayer::Write(ifaces::cIGZOStream *stream)
{
    stream->SetUint32(GetGZCLSID());
    stream->SetUint32(kVersion);
    RecordName name("cWallLayer"); // alive to the end, as the original's
    stream->SetGZStr(&name.mString);
    stream->SetSint32(static_cast<int32_t>(mEntries.size()));
    for (const auto &[key, entry] : mEntries) {
        stream->SetSint32(key);
        stream->SetUint32(entry.first);
        stream->SetUint16(uint16_t(entry.second >> 16));
        stream->SetUint16(uint16_t(entry.second));
    }
    return true;
}

bool cWallLayer::Read(ifaces::cIGZIStream *stream)
{
    // The locals are the original's uninitialised ones (zero here): a failed read compares or stores what they held.
    uint32_t classId = 0;
    stream->GetUint32(&classId);
    if (classId != GetGZCLSID()) return false;
    uint32_t version = 0;
    stream->GetUint32(&version);
    if (version != kVersion) return false;
    RecordName name; // alive to the end, as the original's
    stream->GetGZStr(&name.mString);
    int32_t count = 0;
    stream->GetSint32(&count);
    mEntries.clear();
    // One set of entry locals for the whole loop (the original's): a failed read keeps the previous entry's. The high
    // half goes where the original keeps the stream argument, so before any read it holds the pointer's low half.
    int32_t key = 0;
    uint32_t first = 0;
    uint16_t high = uint16_t(reinterpret_cast<uintptr_t>(stream));
    uint16_t low = 0;
    for (int32_t i = 0; i < count; ++i) {
        stream->GetSint32(&key);
        stream->GetUint32(&first);
        stream->GetUint16(&high);
        stream->GetUint16(&low);
        mEntries[key] = {first, uint32_t(high) << 16 | low};
    }
    return true;
}

// ---- the adapters ----

cWallLayer *__fastcall cWallLayerAdapters::Construct(Self *self, void *) { return new (self) Self(); }
void __fastcall cWallLayerAdapters::DestructBody(Self *self, void *) { self->Self::~Self(); }
cWallLayer *__fastcall cWallLayerAdapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}
bool __fastcall cWallLayerAdapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    return self->Self::QueryInterface(riid, out);
}
bool __fastcall cWallLayerAdapters::Write(Self *self, void *, ifaces::cIGZOStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Write(stream);
}
bool __fastcall cWallLayerAdapters::Read(Self *self, void *, ifaces::cIGZIStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Read(stream);
}
uint32_t __fastcall cWallLayerAdapters::GetGZCLSID(Self *self, void *) { return self->Self::GetGZCLSID(); }
void __fastcall cWallLayerAdapters::Clear(Self *self, void *) { self->Clear(); }
void __fastcall cWallLayerAdapters::Erase(Self *self, void *, int32_t key) { self->Erase(key); }
void __fastcall cWallLayerAdapters::SetHalf(Self *self, void *, int32_t key, int32_t which, const uint16_t *value)
{
    self->SetHalf(key, which, value);
}
bool __fastcall cWallLayerAdapters::Has(Self *self, void *, int32_t key) { return self->Has(key); }
uint16_t __fastcall cWallLayerAdapters::Half(Self *self, void *, int32_t key, int32_t which) { return self->Half(key, which); }
uint32_t __fastcall cWallLayerAdapters::First(Self *self, void *, int32_t key) { return self->First(key); }

} // namespace nTSWorld
