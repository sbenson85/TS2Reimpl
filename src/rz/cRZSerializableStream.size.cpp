#include "rz/cRZSerializableStream.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/cRZIStream.h"
#include "rz/cRZOStream.h"

#include <cstring>
#include <new>

namespace {

using NoArgRet32 = uint32_t(__thiscall *)(void *);
using OneArgRet32 = uint32_t(__thiscall *)(void *, uint32_t);
using OneArgPtr = uint32_t(__thiscall *)(void *, void *);
using TwoArgRet32 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);

// The base streams' vtables (the classes the +0 and +0x10 subobjects derive from).
constexpr uint32_t kOStreamVTable = 0xda29bc;
constexpr uint32_t kIStreamVTable = 0xda2a20;

// The stream's slot 18 (GetError) as a boolean: the original returns !al.
bool NoError(cRZSerializableStream *self)
{
    uint32_t r = VirtualCall<NoArgRet32, cRZSerializableStream::kGetErrorSlot>(self);
    return static_cast<uint8_t>(r) == 0;
}

void SetError(cRZSerializableStream *self, uint32_t flag)
{
    VirtualCall<OneArgRet32, cRZSerializableStream::kSetErrorSlot>(self, flag);
}

// Releases the owner object a base stream holds at +0xc (slot 2, no arguments), if any.
void ReleaseOwner(void *owner)
{
    if (owner) VirtualCall<NoArgRet32, 2>(owner);
}

cRZSerializableStream *Primary(void *subobject, intptr_t offset)
{
    return reinterpret_cast<cRZSerializableStream *>(reinterpret_cast<uintptr_t>(subobject) - offset);
}

// Replaces the buffer with a zeroed one of `bytes` bytes holding the first `size` bytes of the old one.
bool ReplaceBuffer(cRZSerializableStream *s, uint32_t bytes)
{
    uint8_t *old = s->mData;
    uint8_t *fresh = new uint8_t[bytes];
    if (!fresh) return false;
    s->mData = fresh;
    std::memset(fresh, 0, bytes);
    if (old) {
        std::memcpy(s->mData, old, s->mSize);
        delete[] old;
    }
    return true;
}

} // namespace

cRZSerializableStream *cRZSerializableStream::Construct(uint32_t order)
{
    // The two stream base constructors (0x1b4f75 and 0x1b4d82): reference count and owner cleared, the
    // byte-order flag set to the low byte of the argument.
    mRefCount = 0;
    mNativeOrder = static_cast<uint8_t>(order);
    mOutOwner = nullptr;
    mRefCountIn = 0;
    mSkipByteSwap = static_cast<uint8_t>(order);
    mInOwner = nullptr;
    mVTable = GameAddress(kVTable);
    mVTableIn = GameAddress(kVTableIStream);
    mVTableSer = GameAddress(kVTableSerializable);
    mSize = 0;
    mCapacity = 0;
    mPosition = 0;
    mError = 0;
    mData = nullptr;
    SetByteOrder(order);
    return this;
}

void cRZSerializableStream::Destruct()
{
    mVTable = GameAddress(kVTable);
    mVTableIn = GameAddress(kVTableIStream);
    mVTableSer = GameAddress(kVTableSerializable);
    Reset();
    // The input base's destructor, then the output base's.
    mVTableIn = GameAddress(kIStreamVTable);
    ReleaseOwner(mInOwner);
    mVTable = GameAddress(kOStreamVTable);
    ReleaseOwner(mOutOwner);
}

cRZSerializableStream *cRZSerializableStream::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(cRZSerializableStream));
    return this;
}

bool cRZSerializableStream::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kIdSerializableStream || riid == kIdSerializable) {
        // The pointer to the +0x20 subobject, null for a null object.
        uintptr_t self = reinterpret_cast<uintptr_t>(this);
        *out = self ? reinterpret_cast<void *>(self + 0x20) : nullptr;
        VirtualCall<NoArgRet32, kAddRefSlot>(this);
        return true;
    }
    if (reinterpret_cast<cRZOStream *>(this)->QueryInterface(riid, out)) return true;
    return reinterpret_cast<cRZIStream *>(&mVTableIn)->QueryInterface(riid, out);
}

uint32_t cRZSerializableStream::Release()
{
    if (mRefCount > 1) return --mRefCount;
    VirtualCall<OneArgRet32, kReleaseDeleteSlot>(this, 1);
    return 0;
}

bool cRZSerializableStream::SetVoid(const void *src, uint32_t n)
{
    if (src && n) {
        // ORIGINAL-BUG: the result of Grow is ignored, so a failed allocation would copy through the
        // old (too small or null) buffer. Unreachable while operator new[] throws instead of
        // returning null.
        Grow(n);
        std::memcpy(mData + mSize, src, n);
        mSize += n;
    }
    return NoError(this);
}

bool cRZSerializableStream::SetByteOrder(uint32_t order)
{
    if (!mData) {
        mNativeOrder = static_cast<uint8_t>(order);
        mSkipByteSwap = static_cast<uint8_t>(order);
    }
    return true;
}

bool cRZSerializableStream::Reset()
{
    if (mData) {
        delete[] mData;
        mData = nullptr;
    }
    mSize = 0;
    mCapacity = 0;
    mPosition = 0;
    return true;
}

uint32_t cRZSerializableStream::Remaining() const
{
    return mSize - mPosition;
}

bool cRZSerializableStream::Reserve(uint32_t n)
{
    if (mCapacity < n) {
        if (!ReplaceBuffer(this, n)) return false;
        mCapacity = n;
    }
    return true;
}

bool cRZSerializableStream::Grow(uint32_t n)
{
    if (mCapacity - mSize <= n) {
        uint32_t bytes = n + 0x80 + mCapacity;
        if (!ReplaceBuffer(this, bytes)) return false;
        mCapacity += n + 0x80;
    }
    return true;
}

// ---- the cRZIStream subobject --------------------------------------------------------------------

bool cRZSerializableStreamIS::QueryInterface(uint32_t riid, void **out)
{
    return Primary(this, 0x10)->QueryInterface(riid, out);
}

uint32_t cRZSerializableStreamIS::Release()
{
    return Primary(this, 0x10)->Release();
}

bool cRZSerializableStreamIS::Skip(uint32_t n)
{
    cRZSerializableStream *p = Primary(this, 0x10);
    if (p->Remaining() >= n) {
        p->mPosition += n;
        SetError(p, 0);
    } else {
        SetError(p, 1);
    }
    return NoError(p);
}

bool cRZSerializableStreamIS::GetVoid(void *dst, uint32_t n)
{
    cRZSerializableStream *p = Primary(this, 0x10);
    if (p->Remaining() >= n) {
        std::memcpy(dst, p->mData + p->mPosition, n);
        p->mPosition += n;
        SetError(p, 0);
    } else {
        SetError(p, 1);
    }
    return NoError(p);
}

cRZSerializableStream *cRZSerializableStreamIS::vf16_21(uint32_t flags)
{
    return Primary(this, 0x10)->DeletingDestructor(flags);
}

bool cRZSerializableStreamIS::vf16_22(uint32_t order)
{
    return Primary(this, 0x10)->SetByteOrder(order);
}

bool cRZSerializableStreamIS::vf16_23()
{
    return Primary(this, 0x10)->Reset();
}

// ---- the cIGZSerializableStream subobject --------------------------------------------------------

bool cRZSerializableStreamSS::QueryInterface(uint32_t riid, void **out)
{
    return Primary(this, 0x20)->QueryInterface(riid, out);
}

uint32_t cRZSerializableStreamSS::Release()
{
    return Primary(this, 0x20)->Release();
}

bool cRZSerializableStreamSS::Write(void *out)
{
    cRZSerializableStream *p = Primary(this, 0x20);
    if (p->mSize && p->mData) {
        VirtualCall<OneArgRet32, 9>(out, p->mSize);
        if (p->mSize && p->mData) VirtualCall<TwoArgRet32, 17>(out, reinterpret_cast<uint32_t>(p->mData), p->mSize);
    } else {
        VirtualCall<OneArgRet32, 9>(out, 0);
    }
    return true;
}

bool cRZSerializableStreamSS::Read(void *in)
{
    cRZSerializableStream *p = Primary(this, 0x20);
    uint32_t size = 0;
    VirtualCall<OneArgPtr, 9>(in, &size);
    if (p->Reserve(size)) {
        VirtualCall<TwoArgRet32, 17>(in, reinterpret_cast<uint32_t>(p->mData), size);
        p->mSize = size;
    } else {
        SetError(p, 1);
    }
    return true;
}

bool cRZSerializableStreamSS::vf32_8(uint32_t order)
{
    return Primary(this, 0x20)->SetByteOrder(order);
}

bool cRZSerializableStreamSS::vf32_9()
{
    return Primary(this, 0x20)->Reset();
}
