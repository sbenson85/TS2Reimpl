#include "world/c3DArray.h"

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

constexpr const char *kRecordName = "c3DArray";

uint32_t Bits(const void *pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }
int32_t Count(size_t size) { return static_cast<int32_t>(size); }
} // namespace

// ---- the members (one body per copy) ----

// Folded body 0x178b20: only cIGZUnknown (1); a null out answers false.
template <class T> bool c3DArray<T>::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

// Folded body 0x148fb0.
template <class T> uint32_t c3DArray<T>::AddRef() { return ++mRefCount; }

// Folded body 0x4b1d00: at zero the object deletes itself through slot 6 and 0 is returned.
template <class T> uint32_t c3DArray<T>::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

// 0x806aa0 (every copy's slot 5).
template <class T> uint32_t c3DArray<T>::GetGZCLSID() { return kClassId; }

template <class T> c3DArray<T>::~c3DArray() = default;

template <class T> void c3DArray<T>::Resize(int32_t sizeX, int32_t sizeY, int32_t sizeZ)
{
    const uint32_t count = static_cast<uint32_t>(sizeX) * static_cast<uint32_t>(sizeY) * static_cast<uint32_t>(sizeZ);
    mData.resize(count, Element::Default());
    mSizeX = sizeX;
    mSizeY = sizeY;
    mSizeZ = sizeZ;
}

// The class id through the object's own slot 5, version 1, the name, the sizes, the elements. Always true.
template <class T> bool c3DArray<T>::Write(ifaces::cIGZOStream *stream)
{
    stream->SetUint32(GetGZCLSID());
    stream->SetUint32(kVersion);
    RecordName name(kRecordName);
    stream->SetGZStr(&name.mString);
    stream->SetSint32(mSizeX);
    stream->SetSint32(mSizeY);
    stream->SetSint32(mSizeZ);
    Element::WriteAll(stream, mData);
    return true;
}

// The class id is compared with the object's own slot 5 and the version with 1; the name is read and ignored. The sizes
// resize the grid before its elements are read, so a short stream leaves the defaults (or stale values, per copy) behind.
template <class T> bool c3DArray<T>::Read(ifaces::cIGZIStream *stream)
{
    // The byte grid's copy reads the class id into the stream argument's stack slot, the others into a local the original
    // leaves uninitialised: when the read fails the value is the stream pointer (or garbage), never the class id.
    uint32_t classId = Bits(stream);
    stream->GetUint32(&classId);
    if (classId != GetGZCLSID()) return false;
    int32_t version = 0; // uninitialised in the original when the read fails
    stream->GetSint32(&version);
    if (version != static_cast<int32_t>(kVersion)) return false;
    RecordName name;
    stream->GetGZStr(&name.mString);
    int32_t sizeX = 0, sizeY = 0, sizeZ = 0; // as above
    stream->GetSint32(&sizeX);
    stream->GetSint32(&sizeY);
    stream->GetSint32(&sizeZ);
    Resize(sizeX, sizeY, sizeZ);
    Element::ReadAll(stream, mData);
    return true;
}

// ---- the elements ----

void c3DArrayElement<FloorPatternTile>::WriteAll(ifaces::cIGZOStream *stream, const std::vector<FloorPatternTile> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i)
        for (uint16_t segment : data[i].segment) stream->SetUint16(segment);
}
void c3DArrayElement<FloorPatternTile>::ReadAll(ifaces::cIGZIStream *stream, std::vector<FloorPatternTile> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i)
        for (uint16_t &segment : data[i].segment) stream->GetUint16(&segment);
}

void c3DArrayElement<FloorRotationTile>::WriteAll(ifaces::cIGZOStream *stream, const std::vector<FloorRotationTile> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i) stream->SetUint16(data[i]);
}
void c3DArrayElement<FloorRotationTile>::ReadAll(ifaces::cIGZIStream *stream, std::vector<FloorRotationTile> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i) stream->GetUint16(&data[i]);
}

void c3DArrayElement<float>::WriteAll(ifaces::cIGZOStream *stream, const std::vector<float> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i) stream->SetFloat32(data[i]);
}
void c3DArrayElement<float>::ReadAll(ifaces::cIGZIStream *stream, std::vector<float> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i) stream->GetFloat32(&data[i]);
}

void c3DArrayElement<FloorVisibilityTile>::WriteAll(ifaces::cIGZOStream *stream, const std::vector<FloorVisibilityTile> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i)
        for (uint8_t segment : data[i].segment) stream->SetUint8(segment);
}
// Each flag is read into one byte and stored as (byte != 0). The original's byte is the top byte of Read's stream argument
// slot, never reset: a failed read stores the previous flag again, or (stream address >> 24) != 0 when no read succeeded.
void c3DArrayElement<FloorVisibilityTile>::ReadAll(ifaces::cIGZIStream *stream, std::vector<FloorVisibilityTile> &data)
{
    uint8_t flag = static_cast<uint8_t>(Bits(stream) >> 24);
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i)
        for (uint8_t &segment : data[i].segment) {
            stream->GetUint8(&flag);
            segment = flag != 0;
        }
}

void c3DArrayElement<uint8_t>::WriteAll(ifaces::cIGZOStream *stream, const std::vector<uint8_t> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i) stream->SetUint8(data[i]);
}
void c3DArrayElement<uint8_t>::ReadAll(ifaces::cIGZIStream *stream, std::vector<uint8_t> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i) stream->GetUint8(&data[i]);
}

// Per tile: the id count, then the ids.
void c3DArrayElement<ObjectList>::WriteAll(ifaces::cIGZOStream *stream, const std::vector<ObjectList> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i) {
        const ObjectList &ids = data[i];
        const int32_t idCount = Count(ids.size());
        stream->SetSint32(idCount);
        for (int32_t k = 0; k < idCount; ++k) stream->SetSint32(ids[k]);
    }
}
// The id count is read into Read's stream argument slot and the tile's list resized to it (as unsigned) before its ids
// are read. A failed count read keeps the previous tile's count, or the stream's address when none was read.
void c3DArrayElement<ObjectList>::ReadAll(ifaces::cIGZIStream *stream, std::vector<ObjectList> &data)
{
    int32_t idCount = static_cast<int32_t>(Bits(stream));
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i) {
        ObjectList &ids = data[i];
        stream->GetSint32(&idCount);
        ids.resize(static_cast<uint32_t>(idCount));
        for (int32_t k = 0; k < idCount; ++k) stream->GetSint32(&ids[k]);
    }
}

void c3DArrayElement<DeckFoundationTile>::WriteAll(ifaces::cIGZOStream *stream, const std::vector<DeckFoundationTile> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i)
        for (uint32_t segment : data[i].segment) stream->SetUint32(segment);
}
void c3DArrayElement<DeckFoundationTile>::ReadAll(ifaces::cIGZIStream *stream, std::vector<DeckFoundationTile> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i)
        for (uint32_t &segment : data[i].segment) stream->GetUint32(&segment);
}

void c3DArrayElement<AwningTile>::WriteAll(ifaces::cIGZOStream *stream, const std::vector<AwningTile> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i) {
        stream->SetUint32(data[i].first);
        stream->SetUint32(data[i].second);
    }
}
void c3DArrayElement<AwningTile>::ReadAll(ifaces::cIGZIStream *stream, std::vector<AwningTile> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i) {
        stream->GetUint32(&data[i].first);
        stream->GetUint32(&data[i].second);
    }
}

void c3DArrayElement<WallTile>::WriteAll(ifaces::cIGZOStream *stream, const std::vector<WallTile> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i) stream->SetUint8(data[i].bits);
}
// ORIGINAL-BUG: each byte read is ORed into the tile instead of stored, so reading into a grid that already has bits
// (one not freshly resized) keeps them. The byte is a local never reset: a failed read ORs the previous byte again (the
// original's first failure ORs uninitialised stack; ours 0).
void c3DArrayElement<WallTile>::ReadAll(ifaces::cIGZIStream *stream, std::vector<WallTile> &data)
{
    uint8_t bits = 0;
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i) {
        stream->GetUint8(&bits);
        data[i].bits |= bits;
    }
}

void c3DArrayElement<RoomIDTile>::WriteAll(ifaces::cIGZOStream *stream, const std::vector<RoomIDTile> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i)
        for (int16_t segment : data[i].segment) stream->SetSint16(segment);
}
void c3DArrayElement<RoomIDTile>::ReadAll(ifaces::cIGZIStream *stream, std::vector<RoomIDTile> &data)
{
    const int32_t count = Count(data.size());
    for (int32_t i = 0; i < count; ++i)
        for (int16_t &segment : data[i].segment) stream->GetSint16(&segment);
}

// ---- the adapters ----

template <class T> bool __fastcall c3DArrayAdapters<T>::Write(Self *self, void *, ifaces::cIGZOStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Write(stream);
}

template <class T> bool __fastcall c3DArrayAdapters<T>::Read(Self *self, void *, ifaces::cIGZIStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Read(stream);
}

template <class T> uint32_t __fastcall c3DArrayAdapters<T>::GetGZCLSID(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetGZCLSID();
}

// Slot 6 of the game's vtables: the destructor, then the object freed when bit 0 of the flags is set.
template <class T> c3DArray<T> *__fastcall c3DArrayAdapters<T>::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}

// The destructor body alone (cWorldOutsideSupport's destructor calls it on the byte grid it embeds).
template <class T> void __fastcall c3DArrayAdapters<T>::Destruct(Self *self, void *) { self->Self::~Self(); }

template <class T> void __fastcall c3DArrayAdapters<T>::Resize(Self *self, void *, int32_t sizeX, int32_t sizeY, int32_t sizeZ)
{
    [[msvc::forceinline_calls]] self->Self::Resize(sizeX, sizeY, sizeZ);
}

template class c3DArray<FloorPatternTile>;
template class c3DArray<FloorRotationTile>;
template class c3DArray<float>;
template class c3DArray<FloorVisibilityTile>;
template class c3DArray<uint8_t>;
template class c3DArray<ObjectList>;
template class c3DArray<DeckFoundationTile>;
template class c3DArray<AwningTile>;
template class c3DArray<WallTile>;
template class c3DArray<RoomIDTile>;
template struct c3DArrayAdapters<FloorPatternTile>;
template struct c3DArrayAdapters<FloorRotationTile>;
template struct c3DArrayAdapters<float>;
template struct c3DArrayAdapters<FloorVisibilityTile>;
template struct c3DArrayAdapters<uint8_t>;
template struct c3DArrayAdapters<ObjectList>;
template struct c3DArrayAdapters<DeckFoundationTile>;
template struct c3DArrayAdapters<AwningTile>;
template struct c3DArrayAdapters<WallTile>;
template struct c3DArrayAdapters<RoomIDTile>;

} // namespace nTSWorld
