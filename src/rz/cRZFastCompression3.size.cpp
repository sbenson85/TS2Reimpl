#include "rz/cRZFastCompression3.h"

#include "game/game.h"
#include "rz/RefPack.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <new>

namespace {

// The original wraps these in catch(...) and returns false/0 if anything goes wrong. This module was
// built with /EHa (tools/ehinfo.py: FuncInfo flags 0), where that also catches access violations
// from malformed data, so SEH is used here.
bool Decompress(const uint8_t *src, uint32_t *consumed, uint8_t *dst, int32_t capacity, int32_t &result)
{
    __try {
        result = RefPack::Decompress(src, consumed, dst, capacity);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Compress(const uint8_t *src, int32_t length, uint8_t *dst, const int32_t *flags, int32_t &result)
{
    __try {
        result = RefPack::Compress(src, length, dst, flags);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadDecompressedSize(const uint8_t *src, uint32_t &result)
{
    __try {
        result = RefPack::GetDecompressedSize(src);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadPrefix(const void *src, uint32_t &result)
{
    __try {
        result = *static_cast<const uint32_t *>(src);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename Fn> Fn Slot(const void *object, int slot)
{
    return (*reinterpret_cast<Fn *const *>(object))[slot];
}

} // namespace

bool cRZFastCompression3::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid != kGZIID_cIGZUnknown && riid != kGZIID_cIGZCompressor) return false;
    *ppvObj = this;
    Slot<uint32_t(__thiscall *)(void *)>(this, 1)(this); // AddRef
    return true;
}

// Slot 1 (0x148fb0, folded with about 600 unrelated classes): a plain, unlocked increment.
uint32_t cRZFastCompression3::AddRef()
{
    return ++mRefCount;
}

uint32_t cRZFastCompression3::Release()
{
    if (mRefCount > 1) return --mRefCount;
    Slot<void *(__thiscall *)(void *, uint32_t)>(this, 9)(this, 1); // deleting destructor
    return 0;
}

bool cRZFastCompression3::CompressData(const void *src, uint32_t srcLength, void *dst, uint32_t *dstLength)
{
    int32_t quick = mFlag ? 1 : 0;
    int32_t result = 0;
    if (Compress(static_cast<const uint8_t *>(src), static_cast<int32_t>(srcLength), static_cast<uint8_t *>(dst) + 4, &quick, result)) {
        // Total size including the 4-byte length prefix, which is also written to the front of dst.
        *dstLength = static_cast<uint32_t>(result) + 4;
        *static_cast<uint32_t *>(dst) = *dstLength;
    } else {
        *dstLength = 0;
    }
    return *dstLength != 0;
}

bool cRZFastCompression3::DecompressData(const void *src, uint32_t srcLength, void *dst, uint32_t dstCapacity,
                                         uint32_t *dstLength)
{
    int32_t result = 0;
    // The original passes &srcLength as the "consumed" output, overwriting its own argument.
    if (!Decompress(static_cast<const uint8_t *>(src) + 4, &srcLength, static_cast<uint8_t *>(dst),
                    static_cast<int32_t>(dstCapacity), result))
        return false;
    if (result < 0) return false;
    *dstLength = static_cast<uint32_t>(result);
    if (result != 0) return true;
    // Empty output: fine only if the header says the data really is empty (slot 7, through the vtable).
    return Slot<uint32_t(__thiscall *)(void *, const void *)>(this, 7)(this, src) == 0;
}

uint32_t cRZFastCompression3::GetMaxLengthRequiredForCompressedData(uint32_t length)
{
    return length * 15 / 10 + 0x400;
}

uint32_t cRZFastCompression3::GetLengthOfCompressedData(const void *src)
{
    uint32_t result = 0;
    return ReadPrefix(src, result) ? result : 0;
}

uint32_t cRZFastCompression3::GetLengthOfDecompressedData(const void *src)
{
    uint32_t result = 0;
    return ReadDecompressedSize(static_cast<const uint8_t *>(src) + 4, result) ? result : 0;
}

void cRZFastCompression3::GetCompressionVersionString(cIGZString &out)
{
    cRZString version{};
    version.mVTable = cRZString::VTable();
    version.mString = "0.90";
    out.Copy(*version.AsInterface());
}

// 0x9cc320 (thiscall, 2 stack arguments): whether a buffer plausibly holds a compressed block. At least 7 bytes, a
// RefPack header after the 4-byte length prefix, and both lengths (slots 6 and 7, asked of the buffer in that order)
// at most 16 MB.
bool cRZFastCompression3::HasValidHeader(const void *src, uint32_t size)
{
    constexpr uint32_t kMaxLength = 0x1000000;
    if (size < 7) return false;
    if (RefPack::RecognizeHeader(static_cast<const uint8_t *>(src) + 4) == 0) return false;
    uint32_t compressed = Slot<uint32_t(__thiscall *)(void *, const void *)>(this, 6)(this, src);
    uint32_t decompressed = Slot<uint32_t(__thiscall *)(void *, const void *)>(this, 7)(this, src);
    return compressed <= kMaxLength && decompressed <= kMaxLength;
}

void cRZFastCompression3::Destruct()
{
    mVTable = VTable();
}

cRZFastCompression3 *cRZFastCompression3::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

// Our vtable's slots (rzface::cRZFastCompression3, the header): each runs the member of the same name.
namespace rzface {

namespace {
::cRZFastCompression3 *Self(cRZFastCompression3 *face) { return reinterpret_cast<::cRZFastCompression3 *>(face); }
} // namespace

bool cRZFastCompression3::QueryInterface(uint32_t riid, void **ppvObj)
{
    [[msvc::forceinline_calls]] return Self(this)->QueryInterface(riid, ppvObj);
}
uint32_t cRZFastCompression3::AddRef() { return Self(this)->AddRef(); }
uint32_t cRZFastCompression3::Release() { [[msvc::forceinline_calls]] return Self(this)->Release(); }
bool cRZFastCompression3::CompressData(const void *src, uint32_t srcLength, void *dst, uint32_t *dstLength)
{
    [[msvc::forceinline_calls]] return Self(this)->CompressData(src, srcLength, dst, dstLength);
}
bool cRZFastCompression3::DecompressData(const void *src, uint32_t srcLength, void *dst, uint32_t dstCapacity,
                                         uint32_t *dstLength)
{
    [[msvc::forceinline_calls]] return Self(this)->DecompressData(src, srcLength, dst, dstCapacity, dstLength);
}
uint32_t cRZFastCompression3::GetMaxLengthRequiredForCompressedData(uint32_t length)
{
    return Self(this)->GetMaxLengthRequiredForCompressedData(length);
}
uint32_t cRZFastCompression3::GetLengthOfCompressedData(const void *src) { return Self(this)->GetLengthOfCompressedData(src); }
uint32_t cRZFastCompression3::GetLengthOfDecompressedData(const void *src)
{
    return Self(this)->GetLengthOfDecompressedData(src);
}
void cRZFastCompression3::GetCompressionVersionString(cIGZString &out)
{
    [[msvc::forceinline_calls]] Self(this)->GetCompressionVersionString(out);
}
void *cRZFastCompression3::DeletingDestructor(uint32_t flags) { [[msvc::forceinline_calls]] return Self(this)->DeletingDestructor(flags); }

} // namespace rzface
