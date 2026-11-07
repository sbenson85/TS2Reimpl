// nRZGraphic4::cSurfaceLockAsGZBuffer (lane VV): src/gz/cSurfaceLockAsGZBuffer.h.
#include "gz/cSurfaceLockAsGZBuffer.h"

#include "gz/PixelFormat.h" // kIidARGB

namespace nRZGraphic4 {

using nGZGraphic4::cILockableSurface;
using nGZGraphic4::cIPixelFormat;
using nGZGraphic4::cIPixelFormatARGBFace;

namespace {

cILockableSurface *Surface(const nGZGraphic4::cSurfaceLock &lock)
{
    return static_cast<cILockableSurface *>(lock.surface);
}

// The pixel format's ARGB face, let go at once. ORIGINAL-BUG: the image releases the face it asked for before it uses
// it (and uses it whether or not it was found); harmless for the pixel formats, whose counts are fixed.
cIPixelFormatARGBFace *ArgbFace(void *format)
{
    cIPixelFormatARGBFace *argb = nullptr;
    static_cast<cIPixelFormat *>(format)->QueryInterface(nGZGraphic4::kIidARGB, reinterpret_cast<void **>(&argb));
    if (argb) argb->Release();
    return argb;
}

} // namespace

// ==== cGZBufferStub ====

bool cGZBufferStub::QueryInterface(uint32_t riid, void **out)
{
    if (!out || (riid != 1 && riid != cIGZBuffer::kIid)) return false;
    *out = static_cast<cIGZBuffer *>(this);
    AddRef();
    return true;
}

uint32_t cGZBufferStub::AddRef() { return ++mRefCount; }

uint32_t cGZBufferStub::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this; // slot 40, the deleting destructor
    return 0;
}

// ==== cSurfaceLockAsGZBuffer ====

cSurfaceLockAsGZBuffer::cSurfaceLockAsGZBuffer(cILockableSurface *surface)
{
    // The rectangle and row bytes are left as they are: the lock writes them.
    mLock.surface = nullptr;
    mLock.bits = nullptr;
    mLock.pitch = 0;
    mLock.rows = 0;
    surface->Lock(&mLock, false);
}

cSurfaceLockAsGZBuffer::~cSurfaceLockAsGZBuffer()
{
    // The image ends the lock twice (the body, then the lock record's own end) and lets a still-held surface go; the
    // first end always leaves no surface, so the others find nothing to do. A lock that holds no surface is left as it
    // is (its bits, pitch and rows too).
    cILockableSurface *const surface = Surface(mLock);
    if (!surface) return;
    surface->Unlock(&mLock);
    if (cILockableSurface *const held = Surface(mLock)) {
        mLock.surface = nullptr;
        held->Release();
    }
    mLock.bits = nullptr;
    mLock.rowBytes = 0;
    mLock.pitch = 0;
    mLock.rows = 0;
}

bool cSurfaceLockAsGZBuffer::Init(uint32_t, uint32_t, uint32_t, uint32_t) { return false; }
bool cSurfaceLockAsGZBuffer::Shutdown() { return false; }
bool cSurfaceLockAsGZBuffer::Uninitialize() { return false; }
bool cSurfaceLockAsGZBuffer::Lock(uint32_t) { return true; }
bool cSurfaceLockAsGZBuffer::Unlock(uint32_t) { return true; }
bool cSurfaceLockAsGZBuffer::IsLocked() { return mLock.surface != nullptr; }
uint32_t cSurfaceLockAsGZBuffer::GetWidth() { return uint32_t(mLock.rect.right - mLock.rect.left); }
uint32_t cSurfaceLockAsGZBuffer::GetHeight() { return uint32_t(mLock.rect.bottom - mLock.rect.top); }

cGZRect cSurfaceLockAsGZBuffer::GetBounds()
{
    return {mLock.rect.left, mLock.rect.top, mLock.rect.right, mLock.rect.bottom};
}

const cGZRect *cSurfaceLockAsGZBuffer::GetBoundsPtr()
{
    // One rectangle for every buffer (the image's is at 0x10ca820, read by nothing else).
    static cGZRect bounds;
    bounds = GetBounds();
    return &bounds;
}

uint32_t cSurfaceLockAsGZBuffer::GetMemoryUsage() { return 0; }
cGZColorType cSurfaceLockAsGZBuffer::GetColorType() { return GetNativeColorType(); }

cGZColorType cSurfaceLockAsGZBuffer::GetNativeColorType()
{
    return cGZColorType(ColorTypeOf(static_cast<cIPixelFormat *>(Surface(mLock)->GetPixelFormat())));
}

uint32_t cSurfaceLockAsGZBuffer::GetBitsPerPixel()
{
    return uint32_t(static_cast<cIPixelFormat *>(Surface(mLock)->GetPixelFormat())->GetBitsPerPixel());
}

uint32_t cSurfaceLockAsGZBuffer::GetBytesPerPixel()
{
    return uint32_t(static_cast<cIPixelFormat *>(Surface(mLock)->GetPixelFormat())->GetBitsPerPixel() / 8);
}

bool cSurfaceLockAsGZBuffer::Fill(uint32_t, const cGZRect *) { return false; }
bool cSurfaceLockAsGZBuffer::FillCoords(uint32_t, int32_t, int32_t, int32_t, int32_t, const cGZRect *) { return false; }
bool cSurfaceLockAsGZBuffer::FillRect(uint32_t, const cGZRect *, const cGZRect *) { return false; }

uint32_t cSurfaceLockAsGZBuffer::GetPixel(uint32_t x, uint32_t y)
{
    const uint32_t bytes = GetBytesPerPixel();
    const uint8_t *const p = static_cast<const uint8_t *>(mLock.bits) + uint32_t(mLock.pitch) * y + bytes * x;
    switch (bytes) {
    case 1: return p[0];
    case 2: return *reinterpret_cast<const uint16_t *>(p);
    case 3: return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16;
    case 4: return *reinterpret_cast<const uint32_t *>(p);
    default: return 0;
    }
}

void cSurfaceLockAsGZBuffer::SetPixel(uint32_t x, uint32_t y, uint32_t value)
{
    const uint32_t bytes = GetBytesPerPixel();
    uint8_t *const p = static_cast<uint8_t *>(mLock.bits) + uint32_t(mLock.pitch) * y + bytes * x;
    switch (bytes) {
    case 1: p[0] = uint8_t(value); break;
    case 2: *reinterpret_cast<uint16_t *>(p) = uint16_t(value); break;
    case 3:
        p[0] = uint8_t(value);
        p[1] = uint8_t(value >> 8);
        p[2] = uint8_t(value >> 16);
        break;
    case 4: *reinterpret_cast<uint32_t *>(p) = value; break;
    default: break;
    }
}

void cSurfaceLockAsGZBuffer::SetTransparency(uint32_t) {}
bool cSurfaceLockAsGZBuffer::GetTransparency() { return false; }
bool cSurfaceLockAsGZBuffer::IsPixelTransparent(uint32_t, uint32_t) { return false; }
bool cSurfaceLockAsGZBuffer::GetTransparentColor(uint32_t *) { return false; }
void cSurfaceLockAsGZBuffer::RemoveTransparency() {}
bool cSurfaceLockAsGZBuffer::DrawLine(int32_t, int32_t, int32_t, int32_t, uint32_t, int32_t) { return false; }
bool cSurfaceLockAsGZBuffer::Blt(cIGZBuffer *, const cGZRect *, const cGZRect *, const cGZRect *) { return false; }

uint32_t cSurfaceLockAsGZBuffer::ConvertRGBValueToNative(uint8_t r, uint8_t g, uint8_t b)
{
    cIPixelFormatARGBFace *const argb = ArgbFace(Surface(mLock)->GetPixelFormat());
    const uint8_t bgra[4] = {b, g, r, 0xff};
    return argb->PackColor(bgra);
}

bool cSurfaceLockAsGZBuffer::ConvertNativeValueToRGB(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b)
{
    cIPixelFormatARGBFace *const argb = ArgbFace(Surface(mLock)->GetPixelFormat());
    uint8_t bgra[4];
    argb->UnpackColor(bgra, value);
    *r = bgra[2];
    *g = bgra[1];
    *b = bgra[0];
    return true;
}

uint32_t cSurfaceLockAsGZBuffer::ConvertRGBAValueToNative(uint8_t, uint8_t, uint8_t, uint8_t) { return 0; }
bool cSurfaceLockAsGZBuffer::ConvertNativeValueToRGBA(uint32_t, uint8_t *, uint8_t *, uint8_t *, uint8_t *) { return false; }
void *cSurfaceLockAsGZBuffer::GetColorSurfaceBits() { return mLock.bits; }
uint32_t cSurfaceLockAsGZBuffer::GetColorSurfaceStride() { return uint32_t(mLock.pitch); }
uint32_t cSurfaceLockAsGZBuffer::GetChangeCounter() { return 0; }
cIGZUnknown *cSurfaceLockAsGZBuffer::GetHardwareCache() { return nullptr; }
bool cSurfaceLockAsGZBuffer::SetHardwareCache(cIGZUnknown *) { return false; }
bool cSurfaceLockAsGZBuffer::IsReady() { return mLock.surface != nullptr; }

uint32_t cSurfaceLockAsGZBuffer::ColorTypeOf(cIPixelFormat *format)
{
    cIPixelFormatARGBFace *const argb = ArgbFace(format);
    if (!argb) return 0;
    // The image asks the face again for every comparison.
    if (argb->GetBitsPerPixel() == 8) return 2;
    if (argb->GetBitsPerPixel() == 16) {
        if (argb->GetAlphaBits() == 1) return 6;
        if (argb->GetAlphaBits() == 4) return 7;
        if (argb->GetGreenBits() == 6) return 5;
        return argb->GetGreenBits() == 5 ? 4 : 3;
    }
    if (argb->GetBitsPerPixel() == 32) {
        if (argb->GetAlphaBits() == 8) return 9;
        return argb->GetUnusedBits() == 8 ? 8 : 0;
    }
    return 0;
}

} // namespace nRZGraphic4
