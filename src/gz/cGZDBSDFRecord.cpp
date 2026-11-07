#include "gz/cGZDBSDFRecord.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZPersistDBSerialRecord.h"

namespace {

using AddRefFn = uint32_t(__thiscall *)(void *);
using I32Fn = int32_t(__thiscall *)(void *);
using I32ArgFn = int32_t(__thiscall *)(void *, int32_t);
using BoolU32Fn = bool(__thiscall *)(void *, uint32_t);
using U32Fn = uint32_t(__thiscall *)(void *);
using I32I32Fn = int32_t(__thiscall *)(void *, int32_t, int32_t);
// The stream's Read and Write return a bool in al; only al is read back, so the result is kept as a byte.
using ByteDataSizeFn = uint8_t(__thiscall *)(void *, void *, uint32_t);
// ReadWithCount and WriteWithCount take the count by pointer; their own return value is not used.
using VoidDataCountFn = void(__thiscall *)(void *, void *, uint32_t *);

// Stream slots (cRZFile's vtable indices): 6 Position, 7 Length, 13 Seek, 14 Read, 15 ReadWithCount, 16 Write,
// 17 WriteWithCount.
constexpr int kStreamLength = 7;
constexpr int kStreamSeek = 13;
constexpr int kStreamRead = 14;
constexpr int kStreamReadWithCount = 15;
constexpr int kStreamWrite = 16;
constexpr int kStreamWriteWithCount = 17;
// The dword table SeekTo indexes by its origin argument (the original pushes entry [origin] for the stream's Seek).
constexpr uint32_t kSeekOriginTableRva = 0xe5e9f4;

uint8_t *Bytes(void *view)
{
    return static_cast<uint8_t *>(view);
}

// The serial subobject of a cGZDBSDFRecord, at +4.
cGZPersistDBSerialRecord *Serial(cGZDBSDFRecord *record)
{
    return reinterpret_cast<cGZPersistDBSerialRecord *>(reinterpret_cast<uint8_t *>(record) + 4);
}

} // namespace

bool cGZDBSDFRecord::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid != kIdSelf) {
        // The original tail-jumps to the serial subobject's QueryInterface with the same arguments.
        return Serial(this)->QueryInterface(riid, ppvObj);
    }
    *ppvObj = this;
    VirtualCall<AddRefFn, 1>(this); // AddRef through the vtable of this
    return true;
}

uint32_t cGZDBSDFRecord::AddRef()
{
    return Serial(this)->AddRef();
}

uint32_t cGZDBSDFRecord::Release()
{
    return Serial(this)->Release();
}

int32_t cGZDBSDFRecord::GetSize()
{
    if (!mStreamOpen) return -1;
    void *stream = &mStream;
    // The original tail-jumps to the stream's Length, so its result is returned as it is.
    return static_cast<int32_t>(VirtualCall<U32Fn, kStreamLength>(stream));
}

bool cGZDBSDFRecord::GetFieldVoid(void *data, uint32_t size)
{
    if (!mStreamOpen || mSeekMode != 1) return false;
    void *stream = &mStream;
    // The original normalises the stream's result with test/setne, so only al is returned.
    return VirtualCall<ByteDataSizeFn, kStreamRead>(stream, data, size) != 0;
}

bool cGZDBSDFRecord::SetFieldVoid(const void *data, uint32_t size)
{
    if (!mStreamOpen || (mSeekMode != 2 && mSeekMode != 3)) return false;
    void *stream = &mStream;
    return VirtualCall<ByteDataSizeFn, kStreamWrite>(stream, const_cast<void *>(data), size) != 0;
}

uint32_t cGZDBSDFRecord::ReadCounted(void *data, uint32_t count)
{
    if (!mStreamOpen || mSeekMode != 1) return 0;
    void *stream = &mStream;
    // The original keeps the count in a stack local that the stream updates; the local is what it returns.
    uint32_t local = count;
    VirtualCall<VoidDataCountFn, kStreamReadWithCount>(stream, data, &local);
    return local;
}

uint32_t cGZDBSDFRecord::WriteCounted(const void *data, uint32_t count)
{
    if (!mStreamOpen || mSeekMode == 1) return 0;
    void *stream = &mStream;
    uint32_t local = count;
    VirtualCall<VoidDataCountFn, kStreamWriteWithCount>(stream, const_cast<void *>(data), &local);
    return local;
}

bool cGZDBSDFRecord::GetPositionTo(int32_t *out)
{
    if (!mStreamOpen) {
        *out = 0;
        return false;
    }
    void *stream = &mStream;
    *out = VirtualCall<I32Fn, kStreamSlotPosition>(stream);
    return true;
}

bool cGZDBSDFRecord::SeekTo(uint32_t position, uint32_t origin)
{
    if (!mStreamOpen) return false;
    void *stream = &mStream;
    uint32_t length = VirtualCall<U32Fn, kStreamLength>(stream);
    if (position > length) return false;
    // The origin indexes the game's table without a bounds check, as the original does (the valid range of origins
    // is not mapped; the test uses the first eight entries only).
    const uint32_t whence = GameAddress<const uint32_t>(kSeekOriginTableRva)[origin];
    return VirtualCall<I32I32Fn, kStreamSeek>(stream, static_cast<int32_t>(position), static_cast<int32_t>(whence)) != -1;
}

void *cGZDBSDFRecordSerialView::AsIGZPersistDBRecord()
{
    // A null record gives this == 4 (its serial subobject), which maps to null.
    if (reinterpret_cast<uintptr_t>(this) == 4) return nullptr;
    return Bytes(this) + 4;
}

int32_t cGZDBSDFRecordSerialView::GetPosition()
{
    uint8_t *serial = Bytes(this);
    if (serial[0x44] == 0) return -1;
    void *stream = serial + 0x48;
    return VirtualCall<I32Fn, cGZDBSDFRecord::kStreamSlotPosition>(stream);
}

bool cGZDBSDFRecordSerialView::SeekAbsolute(int32_t position)
{
    uint8_t *serial = Bytes(this);
    if (serial[0x44] == 0) return false;
    if (*reinterpret_cast<uint32_t *>(serial + 0x180) != 1) return false;
    void *stream = serial + 0x48;
    return VirtualCall<I32ArgFn, cGZDBSDFRecord::kStreamSlotSeekAbsolute>(stream, position) != -1;
}

bool cGZDBSDFRecordSerialView::SeekRelative(int32_t offset)
{
    uint8_t *serial = Bytes(this);
    if (serial[0x44] == 0) return false;
    if (*reinterpret_cast<uint32_t *>(serial + 0x180) != 1) return false;
    void *stream = serial + 0x48;
    return VirtualCall<I32ArgFn, cGZDBSDFRecord::kStreamSlotSeekRelative>(stream, offset) != -1;
}

bool cGZDBSDFRecordSerialView::SetSize(uint32_t size)
{
    uint8_t *serial = Bytes(this);
    if (serial[0x44] == 0) return false;
    void *stream = serial + 0x48;
    return VirtualCall<BoolU32Fn, cGZDBSDFRecord::kStreamSlotSetLength>(stream, size);
}
