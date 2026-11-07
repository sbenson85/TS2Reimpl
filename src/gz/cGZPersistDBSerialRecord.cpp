#include "gz/cGZPersistDBSerialRecord.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GZCOM.h"
#include "gz/cGZPersistDBRecord.h"
#include "gz/cGZPersistResourceManager.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

#include <cstring>
#include <string>
#include <new>
#include <utility>

namespace {

using RawReadFn = bool(__thiscall *)(void *, void *, uint32_t);
using RefFn = uint32_t(__thiscall *)(void *);

} // namespace

bool cGZPersistDBSerialRecord::RawRead(void *dst, uint32_t n)
{
    return VirtualCall<RawReadFn, kRawReadSlot>(this, dst, n);
}

// Reverses the bytes of each of count elements of width bytes (2, 4 or 8), in place.
void cGZPersistDBSerialRecord::ReverseElements(void *dst, uint32_t count, uint32_t width)
{
    auto *p = static_cast<uint8_t *>(dst);
    for (uint32_t e = 0; e < count; ++e) {
        uint8_t *element = p + static_cast<size_t>(e) * width;
        for (uint32_t i = 0, j = width - 1; i < j; ++i, --j) std::swap(element[i], element[j]);
    }
}

bool cGZPersistDBSerialRecord::QueryInterface(uint32_t riid, void **ppvObj)
{
    uint8_t *self = reinterpret_cast<uint8_t *>(this);
    void *result;
    void *target; // the object AddRef is called on
    switch (riid) {
    case kIdIStream:
        result = self + 0x30;
        target = this;
        break;
    case kIdOStream:
        result = self + 0x20;
        target = this;
        break;
    case kIdSerial:
        result = this;
        target = this;
        break;
    case kIdRecord:
    case kIdFirst:
        result = self + 4;
        target = self + 4;
        break;
    default:
        return false;
    }
    *ppvObj = result;
    VirtualCall<RefFn, 1>(target); // AddRef through the vtable of the subobject
    return true;
}

uint32_t cGZPersistDBSerialRecord::AddRef()
{
    ++mRefCount;
    return mRefCount;
}

uint32_t cGZPersistDBSerialRecord::Release()
{
    if (mRefCount > 1) {
        --mRefCount;
        return mRefCount;
    }
    // Last reference: the cGZPersistDBRecord subobject's deleting destructor, flags 1.
    void *record = reinterpret_cast<uint8_t *>(this) + 4;
    VirtualCall<void(__thiscall *)(void *, uint32_t), kRecordDeleteSlot>(record, 1);
    return 0;
}

bool cGZPersistDBSerialRecord::GetFieldUint8(void *dst, uint32_t count)
{
    return RawRead(dst, count);
}

bool cGZPersistDBSerialRecord::GetFieldUint8(void *dst)
{
    return RawRead(dst, 1);
}

bool cGZPersistDBSerialRecord::GetFieldUint16(void *dst, uint32_t count)
{
    bool ok = RawRead(dst, count * 2);
    if (mSkipByteSwap == 0) ReverseElements(dst, count, 2);
    return ok;
}

bool cGZPersistDBSerialRecord::GetFieldUint16(void *dst)
{
    bool ok = RawRead(dst, 2);
    if (mSkipByteSwap == 0) ReverseElements(dst, 1, 2);
    return ok;
}

bool cGZPersistDBSerialRecord::GetFieldUint32(void *dst, uint32_t count)
{
    bool ok = RawRead(dst, count * 4);
    if (mSkipByteSwap == 0) ReverseElements(dst, count, 4);
    return ok;
}

bool cGZPersistDBSerialRecord::GetFieldUint32(void *dst)
{
    bool ok = RawRead(dst, 4);
    if (mSkipByteSwap == 0) ReverseElements(dst, 1, 4);
    return ok;
}

bool cGZPersistDBSerialRecord::GetFieldUint64(void *dst, uint32_t count)
{
    bool ok = RawRead(dst, count * 8);
    if (mSkipByteSwap == 0) ReverseElements(dst, count, 8);
    return ok;
}

bool cGZPersistDBSerialRecord::GetFieldUint64(void *dst)
{
    bool ok = RawRead(dst, 8);
    if (mSkipByteSwap == 0) ReverseElements(dst, 1, 8);
    return ok;
}

bool cGZPersistDBSerialRecord::GetFieldFloat32(void *dst, uint32_t count)
{
    return RawRead(dst, count * 4);
}

bool cGZPersistDBSerialRecord::GetFieldFloat32(void *dst)
{
    return RawRead(dst, 4);
}

bool cGZPersistDBSerialRecord::GetFieldFloat64(void *dst, uint32_t count)
{
    return RawRead(dst, count * 8);
}

bool cGZPersistDBSerialRecord::GetFieldFloat64(void *dst)
{
    return RawRead(dst, 8);
}

bool cGZPersistDBSerialRecord::GetFieldResKey(void *dst)
{
    using GetUint32Fn = bool(__thiscall *)(void *, void *);
    auto *out = static_cast<uint32_t *>(dst);
    uint32_t value; // stack temporary: a failed read's leftover is never copied out
    if (!VirtualCall<GetUint32Fn, kGetUint32Slot>(this, &value)) return false;
    out[0] = value;
    if (!VirtualCall<GetUint32Fn, kGetUint32Slot>(this, &value)) return false;
    out[1] = value;
    if (!VirtualCall<GetUint32Fn, kGetUint32Slot>(this, &value)) return false;
    out[2] = value;
    return true;
}

bool cGZPersistDBSerialRecord::RawWrite(const void *src, uint32_t n)
{
    using RawWriteFn = bool(__thiscall *)(void *, const void *, uint32_t);
    return VirtualCall<RawWriteFn, kRawWriteSlot>(this, src, n);
}

// Copies count elements of width bytes (2, 4 or 8) into a buffer, reverses each element and writes the
// buffer. Counts up to 0x100 use a stack buffer; larger counts a heap block, freed after the write
// whether or not the write succeeded. The write is made even for a zero count.
bool cGZPersistDBSerialRecord::WriteSwapped(const void *src, uint32_t count, uint32_t width)
{
    const uint32_t bytes = count * width;
    uint8_t stackBuf[0x100 * 8];
    uint8_t *heap = nullptr;
    if (count > 0x100) {
        const uint32_t size = count > 0xffffffffu / width ? 0xffffffffu : bytes;
        heap = static_cast<uint8_t *>(::operator new(size));
    }
    uint8_t *buf = heap ? heap : stackBuf;
    std::memcpy(buf, src, bytes);
    ReverseElements(buf, count, width);
    bool ok = RawWrite(buf, bytes);
    if (heap) ::operator delete(heap);
    return ok;
}

bool cGZPersistDBSerialRecord::SetFieldUint8(const void *src, uint32_t count)
{
    return RawWrite(src, count);
}

bool cGZPersistDBSerialRecord::SetFieldUint8(uint8_t value)
{
    return RawWrite(&value, 1);
}

bool cGZPersistDBSerialRecord::SetFieldUint16(const void *src, uint32_t count)
{
    if (mSkipByteSwap != 0) return RawWrite(src, count * 2);
    return WriteSwapped(src, count, 2);
}

bool cGZPersistDBSerialRecord::SetFieldUint16(uint16_t value)
{
    if (mSkipByteSwap == 0) ReverseElements(&value, 1, 2);
    return RawWrite(&value, 2);
}

bool cGZPersistDBSerialRecord::SetFieldSint16(int16_t value)
{
    uint16_t bits = static_cast<uint16_t>(value);
    if (mSkipByteSwap == 0) ReverseElements(&bits, 1, 2);
    return RawWrite(&bits, 2);
}

bool cGZPersistDBSerialRecord::SetFieldUint32(const void *src, uint32_t count)
{
    if (mSkipByteSwap != 0) return RawWrite(src, count * 4);
    return WriteSwapped(src, count, 4);
}

bool cGZPersistDBSerialRecord::SetFieldUint32(uint32_t value)
{
    if (mSkipByteSwap == 0) ReverseElements(&value, 1, 4);
    return RawWrite(&value, 4);
}

bool cGZPersistDBSerialRecord::SetFieldUint64(const void *src, uint32_t count)
{
    if (mSkipByteSwap != 0) return RawWrite(src, count * 8);
    return WriteSwapped(src, count, 8);
}

bool cGZPersistDBSerialRecord::SetFieldUint64(uint64_t value)
{
    if (mSkipByteSwap == 0) ReverseElements(&value, 1, 8);
    return RawWrite(&value, 8);
}

bool cGZPersistDBSerialRecord::SetFieldFloat32(const void *src, uint32_t count)
{
    return RawWrite(src, count * 4);
}

bool cGZPersistDBSerialRecord::SetFieldFloat32(float value)
{
    return RawWrite(&value, 4);
}

bool cGZPersistDBSerialRecord::SetFieldFloat64(const void *src, uint32_t count)
{
    return RawWrite(src, count * 8);
}

bool cGZPersistDBSerialRecord::SetFieldFloat64(double value)
{
    return RawWrite(&value, 8);
}

bool cGZPersistDBSerialRecord::SetFieldResKey(const void *key)
{
    using SetUint32Fn = bool(__thiscall *)(void *, uint32_t);
    const auto *k = static_cast<const uint32_t *>(key);
    if (!VirtualCall<SetUint32Fn, kSetUint32Slot>(this, k[0])) return false;
    if (!VirtualCall<SetUint32Fn, kSetUint32Slot>(this, k[1])) return false;
    return VirtualCall<SetUint32Fn, kSetUint32Slot>(this, k[2]);
}

// Same bytes as SetFieldUint64: the original builds the 8-byte value in a stack buffer, byte-reversed
// unless the flag is set, and writes it through slot 51.
bool cGZPersistDBSerialRecord::SetFieldSint64(int64_t value)
{
    return SetFieldUint64(static_cast<uint64_t>(value));
}

// Strlen (slot 7), then the length as a Uint32 single (slot 37, through this's vtable). If that fails,
// nothing more happens. Otherwise Data (slot 6) and a raw write of the length in bytes. The write is
// not byte-swapped.
bool cGZPersistDBSerialRecord::SetFieldString(const cIGZString *value)
{
    using StrlenFn = uint32_t(__thiscall *)(void *);
    using DataFn = const char *(__thiscall *)(void *);
    using SetUint32Fn = bool(__thiscall *)(void *, uint32_t);
    void *str = const_cast<void *>(static_cast<const void *>(value));
    const uint32_t length = VirtualCall<StrlenFn, 7>(str);
    if (!VirtualCall<SetUint32Fn, kSetUint32Slot>(this, length)) return false;
    const void *data = VirtualCall<DataFn, 6>(str);
    return RawWrite(data, length);
}

bool cGZPersistDBSerialRecord::GetFieldBlob(void *dst, uint32_t size, uint32_t count)
{
    return RawRead(dst, size * count);
}

bool cGZPersistDBSerialRecord::SetFieldBlob(const void *src, uint32_t size, uint32_t count)
{
    return RawWrite(src, size * count);
}

// Calls this->SetFieldString through the vtable (slot 48) and ignores the result, as the original does.
void *cGZPersistDBSerialRecord::SetFieldStringThenOStream(const cIGZString *value)
{
    using SetStringFn = bool(__thiscall *)(void *, const cIGZString *);
    VirtualCall<SetStringFn, 48>(this, value);
    return reinterpret_cast<uint8_t *>(this) + 0x20;
}

void *cGZPersistDBSerialRecord::GetOStream(uint32_t /*unused*/)
{
    return reinterpret_cast<uint8_t *>(this) + 0x20;
}

// Calls this->GetFieldString through the vtable (slot 24) and ignores the result, as the original does.
void *cGZPersistDBSerialRecord::GetFieldStringThenIStream(cIGZString *dst)
{
    using GetStringFn = bool(__thiscall *)(void *, cIGZString *);
    VirtualCall<GetStringFn, 24>(this, dst);
    return reinterpret_cast<uint8_t *>(this) + 0x30;
}

namespace {

// The primary object of a subobject thunk: the subobject's `this` minus its offset in the primary.
cGZPersistDBSerialRecord *PrimaryOf(const void *subobject, uint32_t offset)
{
    return reinterpret_cast<cGZPersistDBSerialRecord *>(static_cast<uint8_t *>(const_cast<void *>(subobject)) - offset);
}

} // namespace

// +48 (cRZIStream) thunk: the original loads the primary's vtable and tail-jumps to its slot 27.
bool cGZPersistDBSerialRecordThunks::GetVoid(void *dst, uint32_t n)
{
    using RawReadFn = bool(__thiscall *)(void *, void *, uint32_t);
    void *primary = PrimaryOf(this, 0x30);
    return VirtualCall<RawReadFn, cGZPersistDBSerialRecord::kRawReadSlot>(primary, dst, n);
}

// +32 (cRZOStream) thunk: tail-jumps to the primary's slot 51 (raw write).
bool cGZPersistDBSerialRecordThunks::SetVoid(const void *src, uint32_t n)
{
    using RawWriteFn = bool(__thiscall *)(void *, const void *, uint32_t);
    void *primary = PrimaryOf(this, 0x20);
    return VirtualCall<RawWriteFn, cGZPersistDBSerialRecord::kRawWriteSlot>(primary, src, n);
}

// +48 thunk: tail-jumps to the primary's slot 56, which is a shared stub that returns false.
bool cGZPersistDBSerialRecordThunks::Skip(uint32_t n)
{
    using SkipFn = bool(__thiscall *)(void *, uint32_t);
    void *primary = PrimaryOf(this, 0x30);
    return VirtualCall<SkipFn, 56>(primary, n);
}

// +48 thunk: tail-jumps to the primary's slot 24 (GetFieldString).
bool cGZPersistDBSerialRecordThunks::GetGZStr(cIGZString *dst)
{
    using GetStringFn = bool(__thiscall *)(void *, cIGZString *);
    void *primary = PrimaryOf(this, 0x30);
    return VirtualCall<GetStringFn, 24>(primary, dst);
}

// +32 thunk: tail-jumps to the primary's slot 48 (SetFieldString).
bool cGZPersistDBSerialRecordThunks::SetGZStr(const cIGZString *src)
{
    using SetStringFn = bool(__thiscall *)(void *, const cIGZString *);
    void *primary = PrimaryOf(this, 0x20);
    return VirtualCall<SetStringFn, 48>(primary, src);
}

// +4 (cGZPersistDBRecord) thunks: each tail-jumps to the primary's function with the same arguments.
bool cGZPersistDBSerialRecordThunks::QueryInterface4(uint32_t riid, void **ppvObj)
{
    return PrimaryOf(this, 4)->QueryInterface(riid, ppvObj);
}

uint32_t cGZPersistDBSerialRecordThunks::AddRef4()
{
    return PrimaryOf(this, 4)->AddRef();
}

uint32_t cGZPersistDBSerialRecordThunks::Release4()
{
    return PrimaryOf(this, 4)->Release();
}

// +32 and +48 IUnknown thunks: subtract the subobject's offset and tail-jump to the primary's function.
bool cGZPersistDBSerialRecordThunks::QueryInterface32(uint32_t riid, void **ppvObj)
{
    return PrimaryOf(this, 0x20)->QueryInterface(riid, ppvObj);
}

uint32_t cGZPersistDBSerialRecordThunks::AddRef32()
{
    return PrimaryOf(this, 0x20)->AddRef();
}

uint32_t cGZPersistDBSerialRecordThunks::Release32()
{
    return PrimaryOf(this, 0x20)->Release();
}

bool cGZPersistDBSerialRecordThunks::QueryInterface48(uint32_t riid, void **ppvObj)
{
    return PrimaryOf(this, 0x30)->QueryInterface(riid, ppvObj);
}

uint32_t cGZPersistDBSerialRecordThunks::AddRef48()
{
    return PrimaryOf(this, 0x30)->AddRef();
}

uint32_t cGZPersistDBSerialRecordThunks::Release48()
{
    return PrimaryOf(this, 0x30)->Release();
}

// +48 slot 18. The original's shared target is `xor al, al; ret`; see the header.
uint8_t cGZPersistDBSerialRecordThunks::GetError48()
{
    return 0;
}

// The original builds a std::string from text, makes it a cRZString (game vtable, refcount 0), calls the
// primary's slot 48 with it and destroys the string's buffer inline. It does not write the vtable again on the
// way out, so the string is destroyed directly.
bool cGZPersistDBSerialRecordThunks::SetRZCharStr(const char *text)
{
    using SetStringFn = bool(__thiscall *)(void *, const cIGZString *);
    alignas(cRZString) uint8_t raw[sizeof(cRZString)];
    auto *str = reinterpret_cast<cRZString *>(raw);
    str->Construct(text);
    void *primary = PrimaryOf(this, 0x20);
    const bool ok = VirtualCall<SetStringFn, 48>(primary, str->AsInterface());
    str->mString.~basic_string();
    return ok;
}

// ---------------------------------------------------------------------------------------------------------------------
// Constructor, destructor, and the three large variant/string bodies (lane CH). The original translation unit has no C++
// EH, so objects with destructors that live across calls are built in raw storage and destroyed by hand.

// 0x99b7c0. Store order as the original: count/flags dword, the two interface vtables, the key and child, AddRef on the
// child, the two stream constructors (each gets the whole flag dword), the flag byte, then the four final vtables.
cGZPersistDBSerialRecord *cGZPersistDBSerialRecord::Construct(const cGZPersistResourceKey *key, void *child,
                                                              uint32_t skipByteSwap)
{
    mRefCount = 0;
    mRecordFlags = 0;
    mVTable = InterfaceVTable();
    mRecordVTable = BaseRecordVTable();
    std::memcpy(mKey, key, sizeof(mKey));
    mChild = child;
    if (child) VirtualCall<RefFn, 1>(child);
    mOStream.Construct(skipByteSwap);
    mIStream.Construct(skipByteSwap);
    mSkipByteSwap = static_cast<uint8_t>(skipByteSwap);
    mVTable = VTable();
    mRecordVTable = RecordVTable();
    *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(this) + 0x20) = OStreamVTable();
    *reinterpret_cast<const void **>(reinterpret_cast<uint8_t *>(this) + 0x30) = IStreamVTable();
    return this;
}

// 0x1da060.
cGZPersistDBSerialRecord *cGZPersistDBSerialRecord::DeletingDestructor(uint32_t flags)
{
    DestructMembers();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

namespace {

using ReadOneFn = bool(__thiscall *)(void *, void *);
using ReadCountedFn = bool(__thiscall *)(void *, void *, uint32_t);
using SeekAbsoluteFn = bool(__thiscall *)(void *, uint32_t);
using SeekRelativeFn = bool(__thiscall *)(void *, int32_t);
using GetSizeFn = uint32_t(__thiscall *)(void *);
using StringFromCharFn = void(__thiscall *)(void *, const char *, uint32_t);
using StringResizeFn = void(__thiscall *)(void *, uint32_t);
using StringAppendFn = void(__thiscall *)(void *, const char *, uint32_t);
using GetStringFn = bool(__thiscall *)(void *, void *);
using GetTypeFn = uint32_t(__thiscall *)(const void *);
using VariantArrayFn = void(__thiscall *)(void *, const void *, uint32_t);

// Primary vtable slots of the typed readers (single value) used by GetFieldVariant, and the variant's SetVal slots.
constexpr int kReadUint8 = 5, kReadUint16 = 9, kReadUint32 = 13, kReadUint64 = 17;
constexpr int kReadSint8 = 7, kReadSint16 = 11, kReadSint32 = 15, kReadSint64 = 19;
constexpr int kReadFloat32 = 21, kReadFloat64 = 23;
constexpr int kGetStringSlot = 24, kGetSizeSlot = 52, kSeekAbsoluteSlot = 55, kSeekRelativeSlot = 56;
constexpr int kSetStringSlot = 48;

// One scalar case of GetFieldVariant: read a T through this's slot, hand it to the variant's SetVal slot. False when the
// read fails (nothing set).
template <class T, int ReadSlot, int SetSlot> bool ReadScalar(void *self, void *variant)
{
    T value;
    if (!VirtualCall<ReadOneFn, ReadSlot>(self, &value)) return false;
    VirtualCall<void(__thiscall *)(void *, T), SetSlot>(variant, value);
    return true;
}

// One array case of GetFieldVariant: count (slot 13), up to 0x80 elements on the stack or a new[] block, the counted
// read into it (its result kept), the variant's Set<T>Array whatever the read returned (a short read passes whatever the
// buffer holds), then delete[]. The result is the read's.
template <uint32_t ElementSize, int ReadSlot, int SetSlot> bool ReadArray(void *self, void *variant)
{
    uint32_t count;
    if (!VirtualCall<ReadOneFn, cGZPersistDBSerialRecord::kGetUint32Slot>(self, &count)) return false;
    alignas(8) uint8_t stackBuffer[SerialRecordField::kArrayStackElements * 8];
    uint8_t *heap = nullptr;
    if (count > SerialRecordField::kArrayStackElements) {
        // The overflow guard of the original's array new: the size saturates.
        const uint32_t bytes = count > 0xffffffffu / ElementSize ? 0xffffffffu : count * ElementSize;
        heap = new uint8_t[bytes];
    }
    uint8_t *buffer = heap ? heap : stackBuffer;
    const bool ok = VirtualCall<ReadCountedFn, ReadSlot>(self, buffer, count);
    VirtualCall<VariantArrayFn, SetSlot>(variant, buffer, count);
    delete[] heap;
    return ok;
}

// One array case of SetFieldVariant: count (written as a Uint32 through slot 37), then the elements through the writer.
template <int WriteSlot, int GetterSlot> bool WriteArray(void *self, const void *variant)
{
    using WriteFn = bool(__thiscall *)(void *, const void *, uint32_t);
    using ArrayFn = const void *(__thiscall *)(const void *);
    const uint32_t count = VirtualCall<GetTypeFn, 4>(variant);
    if (!VirtualCall<bool(__thiscall *)(void *, uint32_t), cGZPersistDBSerialRecord::kSetUint32Slot>(self, count)) return false;
    return VirtualCall<WriteFn, WriteSlot>(self, VirtualCall<ArrayFn, GetterSlot>(variant), count);
}

} // namespace

// Slot 24, 0x99bec0. Three encodings, told apart by the first dword (read as a Uint32, so byte-swapped unless the flag is
// set): a counted string (top byte 0: bytes, or when the second byte is 0 UTF-16 units; 0x10: UTF-16 units), a 7-bit
// varint length plus bytes, or text (the whole record as raw bytes). A header that cannot be read falls back to reading
// bytes until a read fails.
bool cGZPersistDBSerialRecord::GetFieldString(cIGZString *dst)
{
    uint8_t c = 0;
    uint32_t header = 0;
    if (!VirtualCall<ReadOneFn, kGetUint32Slot>(this, &header)) {
        VirtualCall<SeekAbsoluteFn, kSeekAbsoluteSlot>(this, 0);
        VirtualCall<StringResizeFn, 18>(dst, 0);
        while (VirtualCall<ReadOneFn, kReadUint8>(this, &c)) VirtualCall<StringAppendFn, 20>(dst, reinterpret_cast<const char *>(&c), 1);
        return true;
    }
    const uint32_t encoding = header >> 24;
    if (encoding != 0 && encoding != 0x10) {
        if ((header & 0xff) >= 0x20 && (header & 0xff00) >= 0x2000) {
            // Looks like text: read the whole record again, one byte at a time.
            uint32_t remaining = VirtualCall<GetSizeFn, kGetSizeSlot>(this); // the total size, not what is left
            VirtualCall<SeekRelativeFn, kSeekRelativeSlot>(this, -4);
            VirtualCall<StringResizeFn, 18>(dst, 0);
            // ORIGINAL-BUG: the read's result is ignored, so a failed read appends the previous byte again.
            while (remaining != 0) {
                VirtualCall<ReadOneFn, kReadUint8>(this, &c);
                VirtualCall<StringAppendFn, 20>(dst, reinterpret_cast<const char *>(&c), 1);
                --remaining;
            }
            return true;
        }
        // A varint length (7 bits per byte, at most 5 bytes), then that many bytes.
        VirtualCall<SeekRelativeFn, kSeekRelativeSlot>(this, -4);
        uint32_t length = 0, shift = 0, bytesRead = 0;
        bool ok = true;
        c = 0x80;
        while (c & 0x80) {
            if (!ok) return false;
            ok = VirtualCall<ReadOneFn, kReadUint8>(this, &c);
            ++bytesRead;
            length |= static_cast<uint32_t>(c & 0x7f) << shift;
            shift += 7;
            if (bytesRead >= 5) break;
        }
        if (!ok) return false;
        // ORIGINAL-BUG: the length is not range-checked; a corrupt record asks for a block of up to 4 GB.
        uint8_t *text = new uint8_t[length];
        ok = VirtualCall<ReadCountedFn, kRawReadSlot>(this, text, length);
        if (ok) VirtualCall<StringFromCharFn, 3>(dst, reinterpret_cast<const char *>(text), length);
        delete[] text;
        return ok;
    }
    const uint32_t count = header & 0xffffff;
    if (count > SerialRecordField::kMaxCountedStringUnits) return false;
    const uint32_t bytes = encoding == 0x10 ? count * 2 : count;
    alignas(8) uint8_t stackBuffer[SerialRecordField::kStringStackBytes];
    uint8_t *heap = bytes > SerialRecordField::kStringStackBytes ? new uint8_t[bytes] : nullptr;
    uint8_t *buffer = heap ? heap : stackBuffer;
    bool ok = true;
    if (bytes != 0) ok = VirtualCall<ReadCountedFn, kRawReadSlot>(this, buffer, bytes);
    if (ok) {
        if ((bytes < 2 || buffer[1] != 0) && encoding != 0x10) {
            VirtualCall<StringFromCharFn, 3>(dst, reinterpret_cast<const char *>(buffer), count);
        } else {
            // UTF-16: `count` units (a header with encoding 0 and a zero second byte is taken as UTF-16 too).
            // ORIGINAL-BUG: for encoding 0 only `count` bytes were read, but `count` units (2 * count bytes) are copied,
            // so the copy runs past the data (the stack buffer's remainder, or the end of the heap block).
            alignas(std::u16string) uint8_t raw[sizeof(std::u16string)];
            auto *wide = new (raw) std::u16string;
            wide->assign(reinterpret_cast<const char16_t *>(buffer), count);
            if (mSkipByteSwap == 0) {
                for (size_t i = 0; i < wide->size(); ++i) {
                    const uint16_t unit = static_cast<uint16_t>((*wide)[i]);
                    (*wide)[i] = static_cast<char16_t>((unit >> 8) | (unit << 8));
                }
            }
            ConvertBytesToString(wide->c_str(), static_cast<uint32_t>(wide->size()), 0x10, dst);
            wide->~basic_string();
        }
    }
    delete[] heap;
    return ok;
}

// Slot 26, 0x99c280. Reads a field code (slot 13), builds a cRZVariant (AddRef'd once) and fills it from the value that
// follows; *out is written only on success, otherwise the variant is released and the result is false.
bool cGZPersistDBSerialRecord::GetFieldVariant(cRZVariant **out)
{
    using namespace SerialRecordField;
    uint32_t code;
    if (!VirtualCall<ReadOneFn, kGetUint32Slot>(this, &code)) return false;
    void *memory = ::operator new(0x14);
    void *variant = memory ? static_cast<cIGZVariant *>(::new (memory) cRZVariant) : nullptr;
    VirtualCall<RefFn, 1>(variant); // no null check, as the original
    bool ok = false;
    switch (code) {
    case kEmpty:
        *out = static_cast<cRZVariant *>(variant);
        return true;
    case kUint8: ok = ReadScalar<uint8_t, kReadUint8, 15>(this, variant); break;
    case kUint16: ok = ReadScalar<uint16_t, kReadUint16, 21>(this, variant); break;
    case kUint32: ok = ReadScalar<uint32_t, kReadUint32, 27>(this, variant); break;
    case kUint64: ok = ReadScalar<uint64_t, kReadUint64, 33>(this, variant); break;
    case kSint8: ok = ReadScalar<int8_t, kReadSint8, 18>(this, variant); break;
    case kSint16: ok = ReadScalar<int16_t, kReadSint16, 24>(this, variant); break;
    case kSint32: ok = ReadScalar<int32_t, kReadSint32, 30>(this, variant); break;
    case kSint64: ok = ReadScalar<int64_t, kReadSint64, 36>(this, variant); break;
    case kFloat32: ok = ReadScalar<float, kReadFloat32, 39>(this, variant); break;
    case kFloat64: ok = ReadScalar<double, kReadFloat64, 42>(this, variant); break;
    case kBool: {
        uint8_t flag;
        ok = VirtualCall<ReadOneFn, kReadUint8>(this, &flag);
        if (ok) VirtualCall<void(__thiscall *)(void *, bool), 12>(variant, flag != 0);
        break;
    }
    case kRZChar: ok = ReadScalar<uint8_t, kReadUint8, 51>(this, variant); break;
    case kArray: ok = ReadArray<1, kRawReadSlot, 127>(this, variant); break;
    case kArray | kUint8: ok = ReadArray<1, 4, 99>(this, variant); break;
    case kArray | kUint16: ok = ReadArray<2, 8, 103>(this, variant); break;
    case kArray | kUint32: ok = ReadArray<4, 12, 107>(this, variant); break;
    case kArray | kUint64: ok = ReadArray<8, 16, 111>(this, variant); break;
    case kArray | kSint8: ok = ReadArray<1, 6, 101>(this, variant); break;
    case kArray | kSint16: ok = ReadArray<2, 10, 105>(this, variant); break;
    case kArray | kSint32: ok = ReadArray<4, 14, 109>(this, variant); break;
    case kArray | kSint64: ok = ReadArray<8, 18, 113>(this, variant); break;
    case kArray | kFloat32: ok = ReadArray<4, 20, 115>(this, variant); break;
    case kArray | kFloat64: ok = ReadArray<8, 22, 117>(this, variant); break;
    case kArray | kBool: ok = ReadArray<1, 4, 97>(this, variant); break;
    case kArray | kRZUnicodeChar: ok = ReadArray<2, 8, 121>(this, variant); break;
    case kArray | kChar: ok = ReadArray<1, 4, 119>(this, variant); break;
    case kArray | kRZChar: {
        alignas(cRZString) uint8_t raw[sizeof(cRZString)];
        auto *text = reinterpret_cast<cRZString *>(raw);
        text->Construct();
        ok = VirtualCall<GetStringFn, kGetStringSlot>(this, text->AsInterface());
        VirtualCall<VariantArrayFn, 123>(variant, text->mString.c_str(), static_cast<uint32_t>(text->mString.size()));
        text->Destruct();
        break;
    }
    default: break; // 0xd..0x7fff, 0x800d..0x800f and above 0x8011: unsupported
    }
    if (!ok) {
        VirtualCall<RefFn, 2>(variant); // Release
        return false;
    }
    *out = static_cast<cRZVariant *>(variant);
    return true;
}

// Slot 50, 0x99d660. Writes the field code from the type table (SetFieldUint32 through slot 37), then the value. A tag
// the table maps to a code with no writer (0xd..0xf, 0x12, 0x13) leaves the code written and returns false.
bool cGZPersistDBSerialRecord::SetFieldVariant(const cRZVariant *value)
{
    using namespace SerialRecordField;
    // The narrow setters read their low byte or word of the stack argument; the original pushes the getter's eax as it is.
    using SetUint8Fn = bool(__thiscall *)(void *, uint32_t);
    using SetUint16Fn = bool(__thiscall *)(void *, uint32_t);
    using SetUint32Fn = bool(__thiscall *)(void *, uint32_t);
    using SetUint64Fn = bool(__thiscall *)(void *, uint64_t);
    using SetFloat32Fn = bool(__thiscall *)(void *, float);
    using SetFloat64Fn = bool(__thiscall *)(void *, double);
    using SetStringFn = bool(__thiscall *)(void *, const void *);
    const void *v = value;
    const uint32_t tag = VirtualCall<GetTypeFn, 3>(v);
    const uint32_t *const toFieldCode = ts2_rva_e5e788; // u32[20]; a larger tag reads past it, as the original
    uint32_t code = toFieldCode[tag & 0x7fff];
    if (tag & 0x8000) code |= 0x8000;
    if (!VirtualCall<SetUint32Fn, kSetUint32Slot>(this, code)) return false;
    switch (code) {
    case kEmpty: return true;
    case kUint8: return VirtualCall<SetUint8Fn, 29>(this, VirtualCall<uint32_t(__thiscall *)(const void *), 69>(v));
    case kUint16: return VirtualCall<SetUint16Fn, 33>(this, VirtualCall<uint32_t(__thiscall *)(const void *), 73>(v));
    case kUint32: return VirtualCall<SetUint32Fn, 37>(this, VirtualCall<uint32_t(__thiscall *)(const void *), 77>(v));
    case kUint64: return VirtualCall<SetUint64Fn, 41>(this, VirtualCall<uint64_t(__thiscall *)(const void *), 81>(v));
    case kSint8: return VirtualCall<SetUint8Fn, 31>(this, VirtualCall<uint32_t(__thiscall *)(const void *), 71>(v));
    case kSint16: return VirtualCall<SetUint16Fn, 35>(this, VirtualCall<uint32_t(__thiscall *)(const void *), 75>(v));
    case kSint32: return VirtualCall<SetUint32Fn, 39>(this, VirtualCall<uint32_t(__thiscall *)(const void *), 79>(v));
    case kSint64: return VirtualCall<SetUint64Fn, 43>(this, VirtualCall<uint64_t(__thiscall *)(const void *), 83>(v));
    case kFloat32: return VirtualCall<SetFloat32Fn, 45>(this, VirtualCall<float(__thiscall *)(const void *), 85>(v));
    case kFloat64: return VirtualCall<SetFloat64Fn, 47>(this, VirtualCall<double(__thiscall *)(const void *), 87>(v));
    case kBool: return VirtualCall<SetUint8Fn, 29>(this, VirtualCall<uint32_t(__thiscall *)(const void *), 67>(v));
    case kRZChar:
    case kChar: return VirtualCall<SetUint8Fn, 29>(this, VirtualCall<uint32_t(__thiscall *)(const void *), 93>(v));
    case kRZUnicodeChar: return VirtualCall<SetUint16Fn, 33>(this, VirtualCall<uint32_t(__thiscall *)(const void *), 91>(v));
    case kArray: {
        // GetCount twice (the count, then again for the buffer's size), the void buffer through the raw writer.
        const uint32_t count = VirtualCall<GetTypeFn, 4>(v);
        if (!VirtualCall<SetUint32Fn, kSetUint32Slot>(this, count)) return false;
        const uint32_t size = VirtualCall<GetTypeFn, 4>(v);
        const void *data = VirtualCall<const void *(__thiscall *)(const void *), 128>(v);
        return RawWrite(data, size);
    }
    case kArray | kUint8: return WriteArray<28, 100>(this, v);
    case kArray | kUint16: return WriteArray<32, 104>(this, v);
    case kArray | kUint32: return WriteArray<36, 108>(this, v);
    case kArray | kUint64: return WriteArray<40, 112>(this, v);
    case kArray | kSint8: return WriteArray<30, 102>(this, v);
    case kArray | kSint16: return WriteArray<34, 106>(this, v);
    case kArray | kSint32: return WriteArray<38, 110>(this, v);
    case kArray | kSint64: return WriteArray<42, 114>(this, v);
    case kArray | kFloat32: return WriteArray<44, 116>(this, v);
    case kArray | kFloat64: return WriteArray<46, 118>(this, v);
    case kArray | kBool: return WriteArray<kRawWriteSlot, 98>(this, v);
    case kArray | kRZUnicodeChar: return WriteArray<32, 122>(this, v);
    case kArray | kChar: return WriteArray<28, 120>(this, v);
    case kArray | kRZChar: {
        // A cRZString built from the (pointer, count) pair; SetFieldString writes its own length.
        const uint32_t count = VirtualCall<GetTypeFn, 4>(v);
        const char *data = VirtualCall<const char *(__thiscall *)(const void *), 124>(v);
        alignas(cRZString) uint8_t raw[sizeof(cRZString)];
        auto *text = reinterpret_cast<cRZString *>(raw);
        new (&text->mString) std::string(data, count);
        text->mVTable = cRZString::VTable();
        text->mRefCount = 0;
        const bool result = VirtualCall<SetStringFn, kSetStringSlot>(this, text->AsInterface());
        text->Destruct();
        return result;
    }
    default: return false; // codes with no writer; the code itself has been written
    }
}
