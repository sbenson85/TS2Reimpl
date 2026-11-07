#include "ts/c2DArray.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZSerializable.h"
#include "rz/cRZString.h"

#include <new>

namespace nTSWorld {
namespace {
// The record's name field: a local cRZString of the original, destroyed when the scope ends, also when a stream call throws
// (the original's EH frame).
struct LocalString {
    cRZString mString;
    LocalString() { mString.Construct(); }
    explicit LocalString(const char *text) { mString.Construct(text); }
    ~LocalString() { mString.Destruct(); }
};

constexpr const char *kRecordName = "c2DArray";

uint8_t GetElement(ifaces::cIGZIStream *stream, float *out) { return stream->GetFloat32(out); }
uint8_t GetElement(ifaces::cIGZIStream *stream, uint8_t *out) { return stream->GetUint8(out); }
uint8_t SetElement(ifaces::cIGZOStream *stream, float value) { return stream->SetFloat32(value); }
uint8_t SetElement(ifaces::cIGZOStream *stream, uint8_t value) { return stream->SetUint8(value); }
} // namespace

// float 0x4b1db0, u8 0x4b1d20: the vector takes width * height elements (zero-filled when it grows).
template <class T> void c2DArray<T>::Resize(uint32_t width, uint32_t height)
{
    mData.resize(width * height);
    mHeight = height;
    mWidth = width;
}

// float 0x4b2e90, u8 0x4b2d80: always true; the stream's errors are not looked at.
template <class T> bool c2DArray<T>::Write(ifaces::cIGZOStream *stream)
{
    stream->SetUint32(As<ifaces::cIGZSerializable>(this)->GetGZCLSID());
    stream->SetUint32(kVersion);
    LocalString name(kRecordName);
    stream->SetGZStr(&name.mString);
    stream->SetUint32(mWidth);
    stream->SetUint32(mHeight);
    const int32_t count = static_cast<int32_t>(mData.size());
    for (int32_t i = 0; i < count; ++i) SetElement(stream, mData.data()[i]);
    return true;
}

// float 0x4b0870, u8 0x4b0770: class id and version are checked; the name is read and ignored; the elements are read
// without looking at the stream's errors, so a short stream leaves the zero-filled tail.
template <class T> bool c2DArray<T>::Read(ifaces::cIGZIStream *stream)
{
    // The class id is read into the stack slot of the stream argument: a failing read leaves the stream pointer there.
    uint32_t classId = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stream));
    stream->GetUint32(&classId);
    if (classId != As<ifaces::cIGZSerializable>(this)->GetGZCLSID()) return false;
    int32_t version; // an uninitialised stack local in the original: stays garbage when the read fails
    stream->GetSint32(&version);
    if (version != static_cast<int32_t>(kVersion)) return false;
    LocalString name;
    stream->GetGZStr(&name.mString);
    int32_t width, height; // as above
    stream->GetSint32(&width);
    stream->GetSint32(&height);
    Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    const int32_t count = static_cast<int32_t>(mData.size());
    for (int32_t i = 0; i < count; ++i) GetElement(stream, mData.data() + i);
    return true;
}

// float 0x4ad220, u8 0x4ad200 (the latter calls 0x4acfe0, which stores the u8 vtable and frees the data): the vtable is
// stored first, the data block goes back, and the object itself when bit 0 of the flags is set.
template <class T> c2DArray<T> *c2DArray<T>::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(c2DArray<T>));
    return this;
}

// paint 0x801440 (u8 0x4acfe0): the copy's own vtable goes back in, then the data block is freed.
template <class T> void c2DArray<T>::Destruct()
{
    mVTable = GameAddress<void>(kVTable);
    std::vector<T>().swap(mData);
}

// 0x4af570: slot 5 of all three copies.
template <class T> uint32_t c2DArray<T>::GetGZCLSID() const { return kClassId; }

namespace {
// The version of the terrain record being read (cWorldTerrain::Read 0x8034d0 stores it before it reads the paint).
constexpr uint32_t kTerrainReadVersion = 0x10df504;
constexpr uint32_t kPaintBytes = 3;
} // namespace

// 0x804d40: always true. Each element writes how many of its paint bytes come before the first 0xff, then those bytes.
template <> bool c2DArray<tVertPaintInfo>::Write(ifaces::cIGZOStream *stream)
{
    stream->SetUint32(As<ifaces::cIGZSerializable>(this)->GetGZCLSID());
    stream->SetUint32(kVersion);
    LocalString name(kRecordName);
    stream->SetGZStr(&name.mString);
    stream->SetSint32(static_cast<int32_t>(mWidth));
    stream->SetSint32(static_cast<int32_t>(mHeight));
    const int32_t count = static_cast<int32_t>(mData.size());
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t *paint = mData.data()[i].mPaint;
        uint8_t used = 0;
        while (used < kPaintBytes && paint[used] != 0xff) ++used;
        stream->SetUint8(used);
        for (uint8_t k = 0; k < used; ++k) stream->SetUint8(paint[k]);
    }
    return true;
}

// 0x803340: class id and version checked (false otherwise), the name read and ignored, the array resized to the record's
// width and height (the paint default), then each element's count and bytes; the bytes after them become 0xff and the flag
// 1. The stream's errors are not looked at.
template <> bool c2DArray<tVertPaintInfo>::Read(ifaces::cIGZIStream *stream)
{
    uint32_t classId; // uninitialised stack locals in the original: a failed read leaves them as they were
    stream->GetUint32(&classId);
    if (classId != As<ifaces::cIGZSerializable>(this)->GetGZCLSID()) return false;
    int32_t version;
    stream->GetSint32(&version);
    if (version != static_cast<int32_t>(kVersion)) return false;
    LocalString name;
    stream->GetGZStr(&name.mString);
    int32_t width, height;
    stream->GetSint32(&width);
    stream->GetSint32(&height);
    Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    const bool wide = *GameAddress<uint32_t>(kTerrainReadVersion) < 4;
    // One count and one u16 for the whole loop, as in the original: a failed read leaves the previous element's.
    uint8_t used;
    uint16_t wideByte;
    const int32_t count = static_cast<int32_t>(mData.size());
    for (int32_t i = 0; i < count; ++i) {
        uint8_t *paint = mData.data()[i].mPaint;
        stream->GetUint8(&used);
        uint8_t k = 0;
        // ORIGINAL-BUG: a count over 3 is not clamped; the bytes run on into the following elements (and past the
        // last one's flag, out of the array).
        for (; k < used; ++k) {
            if (wide) {
                stream->GetUint16(&wideByte);
                paint[k] = static_cast<uint8_t>(wideByte);
            } else {
                stream->GetUint8(paint + k);
            }
        }
        for (; k < kPaintBytes; ++k) paint[k] = 0xff;
        mData.data()[i].mFlag = 1;
    }
    return true;
}

template struct c2DArray<float>;
template struct c2DArray<uint8_t>;
template struct c2DArray<tVertPaintInfo>;
} // namespace nTSWorld
