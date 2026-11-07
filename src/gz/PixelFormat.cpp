#include "gz/PixelFormat.h"

#include <utility>

namespace nGZGraphic4 {

namespace {

using Slot0Fn = uint32_t(__thiscall *)(const void *);
using QueryFn = uint32_t(__thiscall *)(const void *, uint32_t, void **);

// Calls through the vtable of an object that may be a game object (slot N is at byte offset 4N).
uint32_t CallU(const void *object, int slot)
{
    return VirtualSlot<Slot0Fn>(object, slot)(object);
}

int32_t CallI(const void *object, int slot)
{
    return static_cast<int32_t>(VirtualSlot<Slot0Fn>(object, slot)(object));
}

// Only al is defined in the byte-returning slots (DepthStencil's float flag).
uint8_t CallB(const void *object, int slot)
{
    return static_cast<uint8_t>(VirtualSlot<Slot0Fn>(object, slot)(object));
}

// QueryInterface(iid) on `other` into a local, then Release of whatever it returned. The reference is
// dropped immediately and the pointer used afterwards, as the original does. Null when the object
// doesn't implement the interface.
const cPixelFormat *QueryAndRelease(const cPixelFormat *other, uint32_t iid)
{
    void *found = nullptr;
    VirtualCall<QueryFn, 0>(other, iid, &found);
    if (!found) return nullptr;
    CallU(found, 2); // Release
    return static_cast<const cPixelFormat *>(found);
}

// The id comparison every Less ends with when the class ids already agree: other's id, then ours.
__forceinline bool IdLess(const cPixelFormat *self, const cPixelFormat *other)
{
    uint32_t theirs = CallU(other, 3);
    uint32_t mine = CallU(self, 3);
    return mine < theirs;
}

} // namespace

// ---------------------------------------------------------------------------------------------------
// cPixelFormat

void cPixelFormat::InitHeader(uint32_t formatId)
{
    mFormatId = formatId;
    new (&mName) std::string();
}

// The original builds "<letter><n>" (insert into the printed number) and appends it in one call.
void cPixelFormat::AppendChannel(const char *letter, uint32_t bits)
{
    std::string text = std::to_string(bits);
    text.insert(0, letter, 1);
    mName.append(text.data(), text.size());
}

cPixelFormat *cPixelFormat::Construct(uint32_t formatId)
{
    InitHeader(formatId);
    mVTable = ClassVTable(kClass);
    std::string number = std::to_string(formatId);
    mName = std::string("Unimplemented:") + std::move(number);
    return this;
}

bool cPixelFormat::QueryInterface(uint32_t riid, void **ppv)
{
    return QueryInterfaceT<kIidPixelFormat>(riid, ppv);
}

uint32_t cPixelFormat::ReturnOne() { return 1; }

uint32_t cPixelFormat::GetFormatId() { return mFormatId; }

const char *cPixelFormat::GetName() { return mName.c_str(); }

uint32_t cPixelFormat::GetClassId() { return kIidPixelFormat; }

uint32_t cPixelFormat::GetRowBytes(uint32_t width) { return width; }

namespace {

// cPixelFormat::Less, which the subclasses' Less inline when the other format isn't their kind.
__forceinline bool BaseLess(const cPixelFormat *self, const cPixelFormat *other, bool strict)
{
    // Class ids first (the slot is called again for the second comparison, as the original does).
    uint32_t theirs = CallU(other, 5);
    uint32_t mine = CallU(self, 5);
    if (mine < theirs) return true;
    theirs = CallU(other, 5);
    mine = CallU(self, 5);
    if (mine > theirs) return false;
    if (strict) return false;
    return IdLess(self, other);
}

} // namespace

bool cPixelFormat::Less(const cPixelFormat *other, bool strict)
{
    return BaseLess(this, other, strict);
}

void *cPixelFormat::DeletingDestructor(uint32_t flags)
{
    return DeletingDestructorT<kClass, kSize>(flags);
}

// ---------------------------------------------------------------------------------------------------
// cPixelFormatABGRFloat

cPixelFormatABGRFloat *cPixelFormatABGRFloat::Construct(uint32_t formatId, uint32_t alphaBits, uint32_t blueBits, uint32_t greenBits, uint32_t redBits)
{
    InitHeader(formatId);
    mBlueBits = blueBits;
    mGreenBits = greenBits;
    mVTable = ClassVTable(kClass);
    mAlphaBits = alphaBits;
    mRedBits = redBits;
    if (mAlphaBits) AppendChannel("A", mAlphaBits);
    if (mBlueBits) AppendChannel("B", mBlueBits);
    if (mGreenBits) AppendChannel("G", mGreenBits);
    if (mRedBits) AppendChannel("R", mRedBits);
    mName.append("F", 1);
    return this;
}

bool cPixelFormatABGRFloat::QueryInterface(uint32_t riid, void **ppv)
{
    return QueryInterfaceRgbT<kIidABGRFloat>(riid, ppv);
}

uint32_t cPixelFormatABGRFloat::GetClassId() { return kIidABGRFloat; }

uint32_t cPixelFormatABGRFloat::GetBitsPerPixel() { return mRedBits + mGreenBits + mBlueBits + mAlphaBits; }

uint32_t cPixelFormatABGRFloat::GetRowBytes(uint32_t width)
{
    uint32_t bits = CallU(this, 6);
    return (bits * width + 7) >> 3;
}

bool cPixelFormatABGRFloat::Less(const cPixelFormat *other, bool strict)
{
    const cPixelFormat *p = QueryAndRelease(other, kIidABGRFloat);
    if (!p) return BaseLess(this, other, strict);
    int32_t theirs;
    theirs = CallI(p, 11);
    if (static_cast<int32_t>(mAlphaBits) < theirs) return true;
    theirs = CallI(p, 11);
    if (static_cast<int32_t>(mAlphaBits) > theirs) return false;
    theirs = CallI(p, 14);
    if (static_cast<int32_t>(mBlueBits) < theirs) return true;
    theirs = CallI(p, 14);
    if (static_cast<int32_t>(mBlueBits) > theirs) return false;
    theirs = CallI(p, 13);
    if (static_cast<int32_t>(mGreenBits) < theirs) return true;
    theirs = CallI(p, 13);
    if (static_cast<int32_t>(mGreenBits) > theirs) return false;
    theirs = CallI(p, 12);
    if (static_cast<int32_t>(mRedBits) < theirs) return true;
    theirs = CallI(p, 12);
    if (static_cast<int32_t>(mRedBits) > theirs) return false;
    if (strict) return false;
    return IdLess(this, other);
}

uint32_t cPixelFormatABGRFloat::GetChannelBitsOr() { return mRedBits | mGreenBits | mBlueBits | mAlphaBits; }

void *cPixelFormatABGRFloat::DeletingDestructor(uint32_t flags)
{
    return DeletingDestructorT<kClass, kSize>(flags);
}

// ---------------------------------------------------------------------------------------------------
// cPixelFormatARGB

cPixelFormatARGB *cPixelFormatARGB::Construct(uint32_t formatId, uint32_t totalBits, uint32_t alphaBits, uint32_t redBits, uint32_t greenBits, uint32_t blueBits)
{
    InitHeader(formatId);
    mAlphaBits = alphaBits;
    mRedBits = redBits;
    mGreenBits = greenBits;
    mVTable = ClassVTable(kClass);
    mTotalBits = totalBits;
    mBlueBits = blueBits;
    std::string prefix = std::to_string(totalBits);
    prefix.insert(0, "BPP", 3);
    mName = std::move(prefix);
    if (mAlphaBits) AppendChannel("A", mAlphaBits);
    if (mRedBits) AppendChannel("R", mRedBits);
    if (mGreenBits) AppendChannel("G", mGreenBits);
    if (mBlueBits) AppendChannel("B", mBlueBits);
    return this;
}

bool cPixelFormatARGB::QueryInterface(uint32_t riid, void **ppv)
{
    return QueryInterfaceRgbT<kIidARGB>(riid, ppv);
}

uint32_t cPixelFormatARGB::GetClassId() { return kIidARGB; }

uint32_t cPixelFormatARGB::GetRowBytes(uint32_t width) { return (mTotalBits * width + 7) >> 3; }

bool cPixelFormatARGB::Less(const cPixelFormat *other, bool strict)
{
    const cPixelFormat *p = QueryAndRelease(other, kIidARGB);
    if (!p) return cPixelFormat::Less(other, strict);
    int32_t theirs;
    theirs = CallI(p, 6);
    if (static_cast<int32_t>(mTotalBits) < theirs) return true;
    theirs = CallI(p, 6);
    if (static_cast<int32_t>(mTotalBits) > theirs) return false;
    theirs = CallI(p, 11);
    if (static_cast<int32_t>(mAlphaBits) < theirs) return true;
    theirs = CallI(p, 11);
    if (static_cast<int32_t>(mAlphaBits) > theirs) return false;
    theirs = CallI(p, 12);
    if (static_cast<int32_t>(mRedBits) < theirs) return true;
    theirs = CallI(p, 12);
    if (static_cast<int32_t>(mRedBits) > theirs) return false;
    theirs = CallI(p, 13);
    if (static_cast<int32_t>(mGreenBits) < theirs) return true;
    theirs = CallI(p, 13);
    if (static_cast<int32_t>(mGreenBits) > theirs) return false;
    theirs = CallI(p, 14);
    if (static_cast<int32_t>(mBlueBits) < theirs) return true;
    theirs = CallI(p, 14);
    if (static_cast<int32_t>(mBlueBits) > theirs) return false;
    // The unused-bits slot is called on both objects.
    theirs = CallI(p, 15);
    int32_t mine = CallI(this, 15);
    if (mine < theirs) return true;
    theirs = CallI(p, 15);
    mine = CallI(this, 15);
    if (mine > theirs) return false;
    if (strict) return false;
    return IdLess(this, other);
}

uint32_t cPixelFormatARGB::GetUnusedBits() { return mTotalBits - mBlueBits - mGreenBits - mRedBits - mAlphaBits; }

namespace {

// round(value * (2^bits - 1) / 255): the original adds 127 and divides (signed) by 255. Shift counts
// are masked to 5 bits as on x86.
uint32_t ScaleChannel(uint8_t value, uint32_t bits)
{
    uint32_t mask = (1u << (bits & 31)) - 1;
    int32_t scaled = static_cast<int32_t>(mask * value + 0x7f);
    return static_cast<uint32_t>(scaled / 255);
}

uint8_t ChannelMask(uint32_t bits)
{
    // 8-bit shift of 1 by the count (masked to 5 bits): zero from bit 8 up, so the mask is then 0xff.
    return static_cast<uint8_t>(static_cast<uint8_t>(1u << (bits & 31)) - 1);
}

} // namespace

uint32_t cPixelFormatARGB::PackColor(const uint8_t *bgra)
{
    uint32_t alpha = ScaleChannel(bgra[3], mAlphaBits) << ((mBlueBits + mGreenBits + mRedBits) & 31);
    uint32_t red = ScaleChannel(bgra[2], mRedBits) << ((mBlueBits + mGreenBits) & 31);
    uint32_t green = ScaleChannel(bgra[1], mGreenBits) << (mBlueBits & 31);
    uint32_t blue = ScaleChannel(bgra[0], mBlueBits);
    return alpha | red | green | blue;
}

uint8_t *cPixelFormatARGB::UnpackColor(uint8_t *bgra, uint32_t pixel)
{
    bgra[3] = static_cast<uint8_t>(pixel >> ((mBlueBits + mGreenBits + mRedBits) & 31)) & ChannelMask(mAlphaBits);
    bgra[2] = static_cast<uint8_t>(pixel >> ((mBlueBits + mGreenBits) & 31)) & ChannelMask(mRedBits);
    bgra[1] = static_cast<uint8_t>(pixel >> (mBlueBits & 31)) & ChannelMask(mGreenBits);
    bgra[0] = static_cast<uint8_t>(pixel) & ChannelMask(mBlueBits);
    return bgra;
}

void *cPixelFormatARGB::DeletingDestructor(uint32_t flags)
{
    return DeletingDestructorT<kClass, kSize>(flags);
}

// ---------------------------------------------------------------------------------------------------
// cPixelFormatAlphaLuminance

cPixelFormatAlphaLuminance *cPixelFormatAlphaLuminance::Construct(uint32_t formatId, uint32_t totalBits, uint32_t alphaBits, uint32_t luminanceBits)
{
    InitHeader(formatId);
    mAlphaBits = alphaBits;
    mVTable = ClassVTable(kClass);
    mTotalBits = totalBits;
    mLuminanceBits = luminanceBits;
    std::string prefix = std::to_string(totalBits);
    prefix.insert(0, "BPP", 3);
    mName = std::move(prefix);
    if (mAlphaBits) AppendChannel("A", mAlphaBits);
    AppendChannel("L", mLuminanceBits);
    return this;
}

bool cPixelFormatAlphaLuminance::QueryInterface(uint32_t riid, void **ppv)
{
    return QueryInterfaceRgbT<kIidAlphaLuminance>(riid, ppv);
}

uint32_t cPixelFormatAlphaLuminance::GetClassId() { return kIidAlphaLuminance; }

uint32_t cPixelFormatAlphaLuminance::GetRowBytes(uint32_t width) { return (mTotalBits * width + 7) >> 3; }

bool cPixelFormatAlphaLuminance::Less(const cPixelFormat *other, bool strict)
{
    const cPixelFormat *p = QueryAndRelease(other, kIidAlphaLuminance);
    if (!p) return cPixelFormat::Less(other, strict);
    int32_t theirs;
    theirs = CallI(p, 6);
    if (static_cast<int32_t>(mTotalBits) < theirs) return true;
    theirs = CallI(p, 6);
    if (static_cast<int32_t>(mTotalBits) > theirs) return false;
    theirs = CallI(p, 11);
    if (static_cast<int32_t>(mAlphaBits) < theirs) return true;
    theirs = CallI(p, 11);
    if (static_cast<int32_t>(mAlphaBits) > theirs) return false;
    theirs = CallI(p, 12);
    if (static_cast<int32_t>(mLuminanceBits) < theirs) return true;
    theirs = CallI(p, 12);
    if (static_cast<int32_t>(mLuminanceBits) > theirs) return false;
    if (strict) return false;
    return IdLess(this, other);
}

void *cPixelFormatAlphaLuminance::DeletingDestructor(uint32_t flags)
{
    return DeletingDestructorT<kClass, kSize>(flags);
}

// ---------------------------------------------------------------------------------------------------
// cPixelFormatDXT

cPixelFormatDXT *cPixelFormatDXT::Construct(uint32_t formatId, uint32_t version)
{
    InitHeader(formatId);
    mVTable = ClassVTable(kClass);
    mBitsPerPixel = 1;
    mAlphaBits = 0;
    mVersion = version;
    switch (version) {
    case 1:
        mBitsPerPixel = 4;
        mAlphaBits = 1;
        break;
    case 2:
    case 3:
        mAlphaBits = 4;
        mBitsPerPixel = 8;
        break;
    case 4:
    case 5:
        mAlphaBits = 8;
        mBitsPerPixel = 8;
        break;
    default:
        break;
    }
    std::string number = std::to_string(version);
    mName = std::string("DXT") + std::move(number);
    return this;
}

bool cPixelFormatDXT::QueryInterface(uint32_t riid, void **ppv)
{
    return QueryInterfaceRgbT<kIidDXT>(riid, ppv);
}

uint32_t cPixelFormatDXT::GetClassId() { return kIidDXT; }

uint32_t cPixelFormatDXT::GetRowBytes(uint32_t width) { return ((width + 3) >> 2) * mBitsPerPixel * 2; }

bool cPixelFormatDXT::Less(const cPixelFormat *other, bool strict)
{
    const cPixelFormat *p = QueryAndRelease(other, kIidDXT);
    if (!p) return cPixelFormat::Less(other, strict);
    // Only the version decides when both are DXT (equal versions are not less, even when not strict).
    return mVersion < CallU(p, 15);
}

uint32_t cPixelFormatDXT::GetBlockBytes() { return mBitsPerPixel * 2; }

void *cPixelFormatDXT::DeletingDestructor(uint32_t flags)
{
    return DeletingDestructorT<kClass, kSize>(flags);
}

// ---------------------------------------------------------------------------------------------------
// cPixelFormatDepthStencil

cPixelFormatDepthStencil *cPixelFormatDepthStencil::Construct(uint32_t formatId, uint32_t totalBits, uint32_t depthBits, uint32_t stencilBits, uint8_t floatDepth)
{
    InitHeader(formatId);
    mTotalBits = totalBits;
    mStencilBits = stencilBits;
    mVTable = ClassVTable(kClass);
    mDepthBits = depthBits;
    mFloatDepth = floatDepth;
    std::string prefix = std::to_string(depthBits);
    prefix.insert(0, "D", 1);
    mName = std::move(prefix);
    if (mFloatDepth) mName.append("F", 1);
    if (mStencilBits) AppendChannel("S", mStencilBits);
    uint32_t unused = mTotalBits - mDepthBits - mStencilBits;
    if (unused) AppendChannel("X", unused);
    return this;
}

bool cPixelFormatDepthStencil::QueryInterface(uint32_t riid, void **ppv)
{
    return QueryInterfaceT<kIidDepthStencil>(riid, ppv);
}

uint32_t cPixelFormatDepthStencil::GetClassId() { return kIidDepthStencil; }

uint32_t cPixelFormatDepthStencil::GetRowBytes(uint32_t width) { return (mTotalBits * width + 7) >> 3; }

bool cPixelFormatDepthStencil::Less(const cPixelFormat *other, bool strict)
{
    const cPixelFormat *p = QueryAndRelease(other, kIidDepthStencil);
    if (!p) return cPixelFormat::Less(other, strict);
    // Integer depth sorts before floating-point depth. Only al of slot 12 is meaningful.
    if (!mFloatDepth) {
        if (CallB(p, 12)) return true;
    } else {
        if (!CallB(p, 12)) return false;
    }
    int32_t theirs;
    theirs = CallI(p, 6);
    int32_t mine = CallI(this, 6);
    if (mine < theirs) return true;
    theirs = CallI(p, 6);
    mine = CallI(this, 6);
    if (mine > theirs) return false;
    theirs = CallI(p, 11);
    if (static_cast<int32_t>(mDepthBits) < theirs) return true;
    theirs = CallI(p, 11);
    if (static_cast<int32_t>(mDepthBits) > theirs) return false;
    theirs = CallI(p, 13);
    if (static_cast<int32_t>(mStencilBits) < theirs) return true;
    theirs = CallI(p, 13);
    if (static_cast<int32_t>(mStencilBits) > theirs) return false;
    if (strict) return false;
    return IdLess(this, other);
}

void *cPixelFormatDepthStencil::DeletingDestructor(uint32_t flags)
{
    return DeletingDestructorT<kClass, kSize>(flags);
}

// ---------------------------------------------------------------------------------------------------
// cPixelFormatUnknown

cPixelFormatUnknown *cPixelFormatUnknown::InitStatic()
{
    InitHeader(0);
    mVTable = ClassVTable(kClass);
    mName = "UNKNOWN";
    return this;
}

bool cPixelFormatUnknown::QueryInterface(uint32_t riid, void **ppv)
{
    return QueryInterfaceT<kIidUnknownFormat>(riid, ppv);
}

uint32_t cPixelFormatUnknown::GetClassId() { return kIidUnknownFormat; }

uint32_t cPixelFormatUnknown::GetRowBytes(uint32_t) { return 0; }

bool cPixelFormatUnknown::Less(const cPixelFormat *other, bool strict)
{
    const cPixelFormat *p = QueryAndRelease(other, kIidUnknownFormat);
    if (!p) return cPixelFormat::Less(other, strict);
    if (strict) return false;
    return IdLess(this, p);
}

void *cPixelFormatUnknown::DeletingDestructor(uint32_t flags)
{
    return DeletingDestructorT<kClass, kSize>(flags);
}

// ---------------------------------------------------------------------------------------------------
// cPixelFormatYUV

cPixelFormatYUV *cPixelFormatYUV::Construct(uint32_t formatId, uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3)
{
    InitHeader(formatId);
    uint32_t fourcc = static_cast<uint8_t>(c3);
    fourcc = (fourcc << 8) | static_cast<uint8_t>(c2);
    fourcc = (fourcc << 8) | static_cast<uint8_t>(c1);
    fourcc = (fourcc << 8) | static_cast<uint8_t>(c0);
    mVTable = ClassVTable(kClass);
    mBitsPerPixel = 1;
    mFourCC = fourcc;
    if (fourcc == 0x32595559 || fourcc == 0x59565955) mBitsPerPixel = 16; // 'YUY2', 'UYVY'
    std::string text(1, static_cast<char>(c0));
    text.push_back(static_cast<char>(c1));
    text.push_back(static_cast<char>(c2));
    text.push_back(static_cast<char>(c3));
    mName = std::move(text);
    return this;
}

bool cPixelFormatYUV::QueryInterface(uint32_t riid, void **ppv)
{
    return QueryInterfaceT<kIidYUV>(riid, ppv);
}

uint32_t cPixelFormatYUV::GetClassId() { return kIidYUV; }

uint32_t cPixelFormatYUV::GetRowBytes(uint32_t width) { return ((width + 1) & 0xfffffffeu) * mBitsPerPixel >> 3; }

bool cPixelFormatYUV::Less(const cPixelFormat *other, bool strict)
{
    const cPixelFormat *p = QueryAndRelease(other, kIidYUV);
    if (!p) return cPixelFormat::Less(other, strict);
    // Only the FOURCC decides when both are YUV; `strict` is not consulted.
    (void)strict;
    return mFourCC < CallU(p, 11);
}

void *cPixelFormatYUV::DeletingDestructor(uint32_t flags)
{
    return DeletingDestructorT<kClass, kSize>(flags);
}

} // namespace nGZGraphic4
