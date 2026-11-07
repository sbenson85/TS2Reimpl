#include "world/cVertexLayer.h"

#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "rz/cRZString.h"

#include <new>

namespace nTSWorld {
namespace {
// The record's name field: a local cRZString of the original, destroyed when the scope ends, also when a stream call
// throws (the original's EH frame).
struct RecordName {
    cRZString mString;
    RecordName() { mString.Construct(); }
    explicit RecordName(const char *text) { mString.Construct(text); }
    ~RecordName() { mString.Destruct(); }
};

constexpr uint32_t kVersion = 1;

// The record's header: the class id against the object's own slot 5, then the version; the name is read and ignored.
// The locals are the original's uninitialised ones (zero here): a failed read compares what they held.
bool ReadHeader(ifaces::cIGZIStream *stream, uint32_t classId, RecordName &name)
{
    uint32_t id = 0;
    stream->GetUint32(&id);
    if (id != classId) return false;
    uint32_t version = 0;
    stream->GetUint32(&version);
    if (version != kVersion) return false;
    stream->GetGZStr(&name.mString);
    return true;
}

void WriteHeader(ifaces::cIGZOStream *stream, uint32_t classId, RecordName &name, size_t count)
{
    stream->SetUint32(classId);
    stream->SetUint32(kVersion);
    stream->SetGZStr(&name.mString);
    stream->SetSint32(static_cast<int32_t>(count));
}

void WriteVertex(ifaces::cIGZOStream *stream, const LayerVertex &vertex)
{
    stream->SetSint32(vertex.level);
    stream->SetFloat32(vertex.x);
    stream->SetFloat32(vertex.y);
}
} // namespace

// ---- the shared part ----

// Folded body 0x148fb0.
template <class Value> uint32_t cVertexMapLayer<Value>::AddRef() { return ++mRefCount; }

// Folded body 0x4b1d00.
template <class Value> uint32_t cVertexMapLayer<Value>::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

template <class Value> bool cVertexMapLayer<Value>::Contains(const LayerVertex &vertex) const
{
    return mMap.find(vertex) != mMap.end();
}

template <class Value> Value cVertexMapLayer<Value>::ValueOf(const LayerVertex &vertex) const
{
    const auto found = mMap.find(vertex);
    // ORIGINAL-BUG: a vertex without an entry answers the head node's value field, which nothing ever writes.
    return found->second;
}

template <class Value> bool cVertexMapLayer<Value>::Vertices(LayerVertexList &out) const
{
    out.clear();
    for (const auto &entry : mMap) out.push_back(entry.first);
    return !mMap.empty();
}

template class cVertexMapLayer<int32_t>;
template class cVertexMapLayer<uint32_t>;

// ---- cVertexLayer ----

bool cVertexLayer::QueryInterface(uint32_t riid, void **out)
{
    if (!out || (riid != 1 && riid != kClassId)) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cVertexLayer::GetGZCLSID() { return kClassId; }

void cVertexLayer::Add(const LayerVertex &vertex, int32_t id)
{
    const auto old = mIds.find(id);
    if (old != mIds.end()) Remove(old->second, old->first);
    const auto [entry, added] = mMap.try_emplace(vertex, 0);
    if (!added) return;
    entry->second = id;
    mIds[id] = vertex; // a new id entry starts as (-1, -1, 0)
}

void cVertexLayer::Remove(const LayerVertex &vertex, int32_t id)
{
    const auto found = mMap.find(vertex);
    if (found == mMap.end() || found->second != id) return;
    mMap.erase(vertex);
    mIds.erase(id);
}

bool cVertexLayer::Write(ifaces::cIGZOStream *stream)
{
    RecordName name("cVertexLayer"); // alive to the end, as the original's
    WriteHeader(stream, GetGZCLSID(), name, mMap.size());
    for (const auto &[vertex, id] : mMap) {
        WriteVertex(stream, vertex);
        stream->SetSint32(id);
    }
    return true;
}

bool cVertexLayer::Read(ifaces::cIGZIStream *stream)
{
    RecordName name; // alive to the end, as the original's
    if (!ReadHeader(stream, GetGZCLSID(), name)) return false;
    int32_t count = 0;
    stream->GetSint32(&count);
    // ORIGINAL-BUG: only the vertex map is emptied; ids the record doesn't mention keep their old vertices.
    mMap.clear();
    // One set of locals for the whole loop (the original's): a failed read keeps the previous entry's value.
    LayerVertex vertex{0.0f, 0.0f, 0};
    int32_t id = 0;
    for (int32_t i = 0; i < count; ++i) {
        stream->GetSint32(&vertex.level);
        stream->GetFloat32(&vertex.x);
        stream->GetFloat32(&vertex.y);
        stream->GetSint32(&id);
        Add(vertex, id);
    }
    return true;
}

// ---- cFencePostLayer ----

bool cFencePostLayer::QueryInterface(uint32_t riid, void **out)
{
    if (!out || (riid != 1 && riid != kClassId)) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cFencePostLayer::GetGZCLSID() { return kClassId; }

void cFencePostLayer::Clear() { mMap.clear(); }

bool cFencePostLayer::Set(const LayerVertex &vertex, uint32_t value) { return mMap.try_emplace(vertex, value).second; }

bool cFencePostLayer::Remove(const LayerVertex &vertex)
{
    const auto found = mMap.find(vertex);
    if (found == mMap.end()) return false;
    mMap.erase(found);
    return true;
}

bool cFencePostLayer::Write(ifaces::cIGZOStream *stream)
{
    RecordName name("cFencePostLayer");
    WriteHeader(stream, GetGZCLSID(), name, mMap.size());
    for (const auto &[vertex, value] : mMap) {
        WriteVertex(stream, vertex);
        stream->SetUint32(value);
    }
    return true;
}

bool cFencePostLayer::Read(ifaces::cIGZIStream *stream)
{
    RecordName name; // alive to the end, as the original's
    if (!ReadHeader(stream, GetGZCLSID(), name)) return false;
    int32_t count = 0;
    stream->GetSint32(&count);
    mMap.clear();
    LayerVertex vertex{0.0f, 0.0f, 0}; // as cVertexLayer::Read
    uint32_t value = 0;
    for (int32_t i = 0; i < count; ++i) {
        stream->GetSint32(&vertex.level);
        stream->GetFloat32(&vertex.x);
        stream->GetFloat32(&vertex.y);
        stream->GetUint32(&value);
        Set(vertex, value);
    }
    return true;
}

// ---- the adapters ----

cVertexLayer *__fastcall cVertexLayerAdapters::Construct(Self *self, void *) { return new (self) Self(); }
void __fastcall cVertexLayerAdapters::Destruct(Self *self, void *) { self->Self::~Self(); }
cVertexLayer *__fastcall cVertexLayerAdapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}
bool __fastcall cVertexLayerAdapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}
bool __fastcall cVertexLayerAdapters::Write(Self *self, void *, ifaces::cIGZOStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Write(stream);
}
bool __fastcall cVertexLayerAdapters::Read(Self *self, void *, ifaces::cIGZIStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Read(stream);
}
uint32_t __fastcall cVertexLayerAdapters::GetGZCLSID(Self *self, void *) { return self->Self::GetGZCLSID(); }
void __fastcall cVertexLayerAdapters::Add(Self *self, void *, const LayerVertex *vertex, int32_t id) { self->Add(*vertex, id); }
void __fastcall cVertexLayerAdapters::Remove(Self *self, void *, const LayerVertex *vertex, int32_t id)
{
    self->Remove(*vertex, id);
}

cFencePostLayer *__fastcall cFencePostLayerAdapters::Construct(Self *self, void *) { return new (self) Self(); }
void __fastcall cFencePostLayerAdapters::Destruct(Self *self, void *) { self->Self::~Self(); }
cFencePostLayer *__fastcall cFencePostLayerAdapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}
bool __fastcall cFencePostLayerAdapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}
bool __fastcall cFencePostLayerAdapters::Write(Self *self, void *, ifaces::cIGZOStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Write(stream);
}
bool __fastcall cFencePostLayerAdapters::Read(Self *self, void *, ifaces::cIGZIStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Read(stream);
}
uint32_t __fastcall cFencePostLayerAdapters::GetGZCLSID(Self *self, void *) { return self->Self::GetGZCLSID(); }
void __fastcall cFencePostLayerAdapters::Clear(Self *self, void *) { self->Self::Clear(); }
bool __fastcall cFencePostLayerAdapters::Set(Self *self, void *, const LayerVertex *vertex, uint32_t value)
{
    return self->Set(*vertex, value);
}
bool __fastcall cFencePostLayerAdapters::Remove(Self *self, void *, const LayerVertex *vertex) { return self->Remove(*vertex); }
bool __fastcall cFencePostLayerAdapters::Contains(Self *self, void *, const LayerVertex *vertex)
{
    return self->Contains(*vertex);
}
uint32_t __fastcall cFencePostLayerAdapters::ValueOf(Self *self, void *, const LayerVertex *vertex)
{
    return self->ValueOf(*vertex);
}
bool __fastcall cFencePostLayerAdapters::Vertices(Self *self, void *, LayerVertexList *out)
{
    return self->Vertices(*out);
}

} // namespace nTSWorld
