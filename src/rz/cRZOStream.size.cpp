#include "rz/cRZOStream.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "rz/RZFrameworkHelpers.h"

#include <cstdlib>
#include <new>

namespace {

constexpr int kSerializeSlot = 3;
constexpr int kStrlenSlot = 7;
constexpr int kToCharSlot = 5;

using NoArgRet32 = uint32_t(__thiscall *)(void *);
using OneArgRet32 = uint32_t(__thiscall *)(void *, uint32_t);

} // namespace

cRZOStream *cRZOStream::Construct(uint32_t order)
{
    mRefCount = 0;
    mVTable = VTable();
    mNativeOrder = static_cast<uint8_t>(order);
    mOwner = nullptr;
    SetByteOrder(order);
    return this;
}

void cRZOStream::Destruct()
{
    mVTable = VTable();
    if (mOwner) VirtualCall<NoArgRet32, kReleaseSlot>(mOwner);
}

cRZOStream *cRZOStream::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cRZOStream));
    return this;
}

bool cRZOStream::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid != kGZIID_cIGZUnknown && riid != kGZIID_Unidentified) return false;
    *ppvObj = this;
    VirtualCall<NoArgRet32, kAddRefSlot>(this);
    return true;
}

uint32_t cRZOStream::Release()
{
    if (mRefCount > 1) return --mRefCount;
    VirtualCall<OneArgRet32, kDeletingDestructorSlot>(this, 1);
    return 0;
}

uint32_t cRZOStream::WriteRaw(const void *buf, uint32_t size)
{
    return VirtualCall<uint32_t(__thiscall *)(void *, const void *, uint32_t), kRawWriteSlot>(this, buf, size);
}

uint32_t cRZOStream::SetSint8(uint8_t value)
{
    return WriteRaw(&value, 1);
}

uint32_t cRZOStream::SetSint16(int16_t value)
{
    uint16_t v = static_cast<uint16_t>(value);
    if (!mNativeOrder) v = _byteswap_ushort(v);
    return WriteRaw(&v, 2);
}

uint32_t cRZOStream::SetUint16(uint16_t value)
{
    uint16_t v = value;
    if (!mNativeOrder) v = _byteswap_ushort(v);
    return WriteRaw(&v, 2);
}

uint32_t cRZOStream::SetSint32(int32_t value)
{
    uint32_t v = static_cast<uint32_t>(value);
    if (!mNativeOrder) v = _byteswap_ulong(v);
    return WriteRaw(&v, 4);
}

uint32_t cRZOStream::SetSint64(int64_t value)
{
    uint64_t v = static_cast<uint64_t>(value);
    if (!mNativeOrder) v = ByteSwap64(v);
    return WriteRaw(&v, 8);
}

uint32_t cRZOStream::SetFloat32(float value)
{
    return WriteRaw(&value, 4);
}

uint32_t cRZOStream::SetFloat64(double value)
{
    return WriteRaw(&value, 8);
}

uint32_t cRZOStream::SetPackedUint32(uint32_t value)
{
    auto setUint8 = VirtualSlot<OneArgRet32>(this, kSetUint8Slot);
    if (value == 0) return setUint8(this, 0);
    uint32_t rest = value >> 7;
    uint32_t last = value & 0x7f;
    while (rest != 0) {
        setUint8(this, last | 0x80);
        last = rest & 0x7f;
        rest >>= 7;
    }
    // The final argument is `value` with its low byte replaced (the original stores the byte back
    // into its stack argument and pushes the whole dword).
    return setUint8(this, (value & 0xffffff00u) | last);
}

uint8_t cRZOStream::SetRZCharStr(const char *text)
{
    uint32_t length = 0;
    while (text[length]) ++length;
    SetPackedUint32(length);
    if (length == 0) return 1;
    return static_cast<uint8_t>(WriteRaw(text, length));
}

uint8_t cRZOStream::SetGZStr(cIGZString *text)
{
    uint32_t length = VirtualCall<NoArgRet32, kStrlenSlot>(text);
    SetPackedUint32(length);
    if (length == 0) return 1;
    const void *chars = reinterpret_cast<const void *>(static_cast<uintptr_t>(VirtualCall<NoArgRet32, kToCharSlot>(text)));
    return static_cast<uint8_t>(WriteRaw(chars, length));
}

uint32_t cRZOStream::SetGZSerializable(void *serializable)
{
    return VirtualCall<uint32_t(__thiscall *)(void *, void *), kSerializeSlot>(serializable, this);
}

void *cRZOStream::SetOwner(void *owner)
{
    return reinterpret_cast<RZUnknownSlot *>(&mOwner)->Assign(owner);
}

bool cRZOStream::SetByteOrder(uint32_t order)
{
    mNativeOrder = static_cast<uint8_t>(order);
    return true;
}

uint64_t __stdcall cRZOStream::ByteSwap64(uint64_t value)
{
    return _byteswap_uint64(value);
}
