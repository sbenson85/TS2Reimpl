#include "rz/cRZIStream.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/cRZOStream.h"
#include "rz/cRZString.h"

#include <cstring>
#include <new>
#include <utility>

namespace {

constexpr int kFromCharSlot = 4;
constexpr int kAppendSlot = 20; // Append(const char *, uint32_t)

using NoArgRet32 = uint32_t(__thiscall *)(void *);

// Reads n bytes through the object's own raw-read slot (`call [eax+0x44]` in the original).
bool GetVoid(const cRZIStream *stream, void *dst, uint32_t n)
{
    return VirtualSlot<bool(__thiscall *)(void *, void *, uint32_t)>(stream, cRZIStream::kRawReadSlot)(
        const_cast<cRZIStream *>(stream), dst, n);
}

// Reverses the order of n bytes in place (what the original's bswap does to a 2- or 4-byte value).
void ReverseBytes(void *dst, int n)
{
    auto *p = static_cast<uint8_t *>(dst);
    for (int i = 0, j = n - 1; i < j; ++i, --j) std::swap(p[i], p[j]);
}

bool HasError(cRZIStream *s)
{
    return static_cast<uint8_t>(VirtualCall<NoArgRet32, cRZIStream::kGetErrorSlot>(s)) != 0;
}

} // namespace

cRZIStream *cRZIStream::Construct(uint32_t order)
{
    mRefCount = 0;
    mVTable = VTable();
    mSkipByteSwap = static_cast<uint8_t>(order);
    mOwner = nullptr;
    return this;
}

void cRZIStream::Destruct()
{
    mVTable = VTable();
    if (mOwner) VirtualCall<NoArgRet32, kReleaseSlot>(mOwner);
}

cRZIStream *cRZIStream::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cRZIStream));
    return this;
}

bool cRZIStream::QueryInterface(uint32_t id, void **out)
{
    if (id != kIdFirst && id != kIdSecond) return false;
    *out = this;
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
    return true;
}

bool cRZIStream::GetSint8(void *dst)
{
    return GetVoid(this, dst, 1);
}

bool cRZIStream::GetSint16(void *dst)
{
    if (!GetVoid(this, dst, 2)) return false;
    if (mSkipByteSwap == 0) ReverseBytes(dst, 2);
    return true;
}

bool cRZIStream::GetSint32(void *dst)
{
    if (!GetVoid(this, dst, 4)) return false;
    if (mSkipByteSwap == 0) ReverseBytes(dst, 4);
    return true;
}

bool cRZIStream::GetSint64(void *dst)
{
    if (!GetVoid(this, dst, 8)) return false;
    if (mSkipByteSwap == 0) {
        uint64_t v;
        std::memcpy(&v, dst, 8);
        v = cRZOStream::ByteSwap64(v);
        std::memcpy(dst, &v, 8);
    }
    return true;
}

bool cRZIStream::GetFloat32(void *dst)
{
    return GetVoid(this, dst, 4);
}

bool cRZIStream::GetFloat64(void *dst)
{
    return GetVoid(this, dst, 8);
}

uint32_t cRZIStream::GetPackedUint32(uint32_t *out, void * /*unused*/)
{
    // The original's 8-byte buffer starts as two pushed copies of `this`. It is bigger here so that a
    // long run of continuation bytes does not write outside it (the original corrupts its stack).
    uint8_t bytes[16];
    uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    std::memcpy(bytes, &self, 4);
    std::memcpy(bytes + 4, &self, 4);
    std::memset(bytes + 8, 0, 8);
    *out = 0;
    auto getUint8 = VirtualSlot<uint32_t(__thiscall *)(void *, void *)>(this, kGetUint8Slot);
    getUint8(this, &bytes[0]);
    if (bytes[0] != 0) {
        int last = 0;
        if (bytes[0] & 0x80) {
            do {
                bytes[last] &= 0x7f;
                ++last;
                getUint8(this, &bytes[last]);
            } while (last < 15 && (bytes[last] & 0x80));
        }
        uint32_t value = 0;
        for (int i = last; i >= 0; --i) value = (value << 7) | bytes[i];
        *out = value;
    }
    return 0;
}

uint8_t cRZIStream::GetRZCharStr(char *buf, uint32_t size)
{
    uint32_t length;
    GetPackedUint32(&length, &length);
    if (HasError(this) || length == 0) {
        buf[0] = 0;
    } else {
        uint32_t count = length < size ? length : size;
        if (count > 0) GetVoid(this, buf, count);
        buf[count] = 0;
        if (!HasError(this) && length - count != 0) {
            VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), kSkipSlot>(this, length - count);
        }
    }
    return HasError(this) ? 0 : 1;
}

uint8_t cRZIStream::GetGZStr(cIGZString *out)
{
    VirtualCall<uint32_t(__thiscall *)(void *, const char *), kFromCharSlot>(out, ts2_rva_d9b9d0);
    uint32_t length;
    GetPackedUint32(&length, &length);
    if (!HasError(this)) {
        // ORIGINAL-BUG: the chunk buffer is not initialised, so bytes a failed read left unwritten
        // are appended as whatever the stack held (zeros here).
        uint8_t chunk[0x100];
        std::memset(chunk, 0, sizeof(chunk));
        uint32_t remaining = length;
        while (remaining != 0) {
            uint32_t count = remaining < 0x100 ? remaining : 0xff;
            GetVoid(this, chunk, count);
            chunk[count] = 0;
            VirtualCall<uint32_t(__thiscall *)(void *, const void *, uint32_t), kAppendSlot>(out, chunk, count);
            remaining -= count;
            if (HasError(this)) break;
        }
    }
    return HasError(this) ? 0 : 1;
}

uint32_t cRZIStream::GetGZSerializable(void *serializable)
{
    return VirtualCall<uint32_t(__thiscall *)(void *, void *), 4>(serializable, this);
}
