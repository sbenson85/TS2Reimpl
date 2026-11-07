#include "gz/cGZDrawContextAsBuffer.h"

#include "game/game.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"

#include <new>

namespace {

// Sums and differences wrap as the original's 32-bit add / sub do.
int32_t Plus(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); }
int32_t Minus(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }
int32_t Abs(int32_t a) { return a < 0 ? Minus(0, a) : a; }

// An unsigned count as a float, as the original converts it (through a double, which holds it exactly).
float UnsignedToFloat(uint32_t n) { return static_cast<float>(static_cast<double>(n)); }

} // namespace

// ---- construction, destruction, the pool ----

cGZDrawContextAsBuffer::cGZDrawContextAsBuffer()
{
    // As 0x8c13a0: the bounds, the format, the surface's other words, the colour and the rebatch flag stay as the block
    // had them.
    mContext = nullptr;
    mBuffer = nullptr;
    mShader = nullptr;
    mLockCount = 0;
    mTransparent = false;
    mSurface.bits = nullptr;
    mSurface.format = 1;
}

cGZDrawContextAsBuffer::~cGZDrawContextAsBuffer()
{
    if (mShader) mShader->Release();
    if (mBuffer) mBuffer->Release();
    if (mContext) mContext->Release();
}

void *cGZDrawContextAsBuffer::operator new(size_t size)
{
    if (size != kSize) return ::operator new(size);
    return GameAddress<RZFixedAllocatorBase>(kPool)->Alloc();
}

void cGZDrawContextAsBuffer::operator delete(void *block, size_t size)
{
    if (size != kSize) {
        ::operator delete(block);
        return;
    }
    GameAddress<RZFastFixed0x58>(kPool)->Free(block);
}

cIGZDrawContextAsBuffer *cGZDrawContextAsBuffer::Create()
{
    cGZDrawContextAsBuffer *const object = new cGZDrawContextAsBuffer;
    return object ? static_cast<cIGZDrawContextAsBuffer *>(object) : nullptr;
}

cGZDrawContextAsBuffer *__fastcall cGZDrawContextAsBufferAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZDrawContextAsBuffer;
}

bool __fastcall cGZDrawContextAsBufferAdapters::DrawEllipse(cGZDrawContextAsBuffer *self, void *, int32_t left,
                                                             int32_t top, int32_t right, int32_t bottom, uint32_t value,
                                                             int32_t thickness)
{
    return self->DrawEllipse(left, top, right, bottom, value, thickness);
}

// ---- cIGZUnknown ----

bool cGZDrawContextAsBuffer::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case 1:
    case cIGZBuffer::kIid:
        *out = static_cast<cIGZBuffer *>(this);
        AddRef();
        return true;
    case cIGZBufferShader::kIid:
        *out = static_cast<cIGZBufferShader *>(this);
        AddRef();
        return true;
    case cIGZDrawContextAsBuffer::kIid:
        *out = static_cast<cIGZDrawContextAsBuffer *>(this);
        AddRef();
        return true;
    default:
        if (mBuffer) return mBuffer->QueryInterface(riid, out);
        return false;
    }
}

uint32_t cGZDrawContextAsBuffer::AddRef() { return rz::cRZRefCount::AddRef(); }

uint32_t cGZDrawContextAsBuffer::Release() { return rz::cRZRefCount::Release(); }

// ---- cIGZBuffer ----

bool cGZDrawContextAsBuffer::Init(uint32_t, uint32_t, uint32_t, uint32_t) { return false; }

bool cGZDrawContextAsBuffer::Shutdown() { return true; }

bool cGZDrawContextAsBuffer::Uninitialize() { return false; }

bool cGZDrawContextAsBuffer::Lock(uint32_t flags)
{
    if (mLockCount == 0) {
        const bool write = (flags & kLockWrite) != 0;
        mRebatch = write && (!mContext->HasTarget() || mContext->InBatch());
        mContext->SetTarget(mBuffer);
        if (write && mContext->InBatch()) mContext->EndBatch();
        mContext->LockSurface(&mSurface, write ? 0 : 1);
    }
    ++mLockCount;
    return true;
}

bool cGZDrawContextAsBuffer::Unlock(uint32_t)
{
    if (--mLockCount == 0) {
        mContext->UnlockSurface();
        if (mRebatch) mContext->BeginBatch();
    }
    return true;
}

bool cGZDrawContextAsBuffer::IsLocked() { return mLockCount != 0; }

uint32_t cGZDrawContextAsBuffer::GetWidth()
{
    const cGZRect *bounds = GetBoundsPtr();
    return static_cast<uint32_t>(Minus(bounds->right, bounds->left));
}

uint32_t cGZDrawContextAsBuffer::GetHeight()
{
    const cGZRect *bounds = GetBoundsPtr();
    return static_cast<uint32_t>(Minus(bounds->bottom, bounds->top));
}

cGZRect cGZDrawContextAsBuffer::GetBounds() { return mBounds; }

const cGZRect *cGZDrawContextAsBuffer::GetBoundsPtr() { return &mBounds; }

uint32_t cGZDrawContextAsBuffer::GetMemoryUsage() { return 0; }

cGZColorType cGZDrawContextAsBuffer::GetColorType() { return cGZColorType(mFormat); }

cGZColorType cGZDrawContextAsBuffer::GetNativeColorType() { return cGZColorType(mFormat); }

uint32_t cGZDrawContextAsBuffer::GetBitsPerPixel() { return mSurface.bytesPerPixel << 3; }

uint32_t cGZDrawContextAsBuffer::GetBytesPerPixel() { return mSurface.bytesPerPixel; }

bool cGZDrawContextAsBuffer::Fill(uint32_t value, const cGZRect *clip)
{
    mContext->PushTransform();
    if (clip) mContext->IntersectClip(clip);
    mContext->SetColorAt(0, NativeToArgb(value));
    mContext->FillClip();
    mContext->PopTransform();
    mContext->vf41();
    return true;
}

bool cGZDrawContextAsBuffer::FillCoords(uint32_t value, int32_t left, int32_t top, int32_t right, int32_t bottom,
                                        const cGZRect *clip)
{
    const cGZRect rect{left, top, right, bottom};
    return FillRect(value, &rect, clip);
}

bool cGZDrawContextAsBuffer::FillRect(uint32_t value, const cGZRect *rect, const cGZRect *clip)
{
    mContext->PushTransform();
    if (clip) mContext->IntersectClip(clip);
    mContext->SetColorAt(0, NativeToArgb(value));
    mContext->FillRect(rect);
    mContext->PopTransform();
    mContext->vf41();
    return true;
}

uint32_t cGZDrawContextAsBuffer::GetPixel(uint32_t x, uint32_t y)
{
    // The context's surface is locked for reading and not unlocked again (as the original).
    cGZDrawSurface surface;
    if (!mContext->LockSurface(&surface, 1)) return 0;
    const uint8_t *row = surface.bits + static_cast<int32_t>(static_cast<uint32_t>(surface.stride) * y);
    switch (surface.bytesPerPixel) {
    case 1: return row[x];
    case 2: return reinterpret_cast<const uint16_t *>(row)[x];
    case 3: {
        const uint8_t *p = row + x * 3;
        return (static_cast<uint32_t>(p[2]) << 16) + (static_cast<uint32_t>(p[1]) << 8) + p[0];
    }
    case 4: return reinterpret_cast<const uint32_t *>(row)[x];
    default: return 0;
    }
}

void cGZDrawContextAsBuffer::SetPixel(uint32_t x, uint32_t y, uint32_t value)
{
    mContext->SetColorAt(0, NativeToArgb(value));
    mContext->DrawPoint(static_cast<int32_t>(x), static_cast<int32_t>(y));
    mContext->vf41();
}

void cGZDrawContextAsBuffer::SetTransparency(uint32_t colour)
{
    mTransparent = true;
    mBuffer->SetTransparency(colour);
}

bool cGZDrawContextAsBuffer::GetTransparency() { return mTransparent; }

bool cGZDrawContextAsBuffer::IsPixelTransparent(uint32_t x, uint32_t y)
{
    cGZPoint origin;
    mContext->GetOrigin(&origin);
    uint32_t colour;
    const bool has = mBuffer->GetTransparentColor(&colour);
    x += static_cast<uint32_t>(origin.x);
    y += static_cast<uint32_t>(origin.y);
    if (!has || x >= mSurface.width || y >= mSurface.height) return false;
    uint32_t mask;
    switch (mSurface.format) {
    case 4:
    case 6: mask = 0x7fff; break;
    case 5: mask = 0xffff; break;
    case 7: mask = 0xfff; break;
    case 8:
    case 9: mask = 0xffffff; break;
    default: mask = 0xffffffff; break;
    }
    return ((GetPixel(x, y) ^ colour) & mask) == 0;
}

bool cGZDrawContextAsBuffer::GetTransparentColor(uint32_t *colour) { return mBuffer->GetTransparentColor(colour); }

void cGZDrawContextAsBuffer::RemoveTransparency()
{
    mTransparent = false;
    mBuffer->RemoveTransparency();
}

bool cGZDrawContextAsBuffer::DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t colour,
                                      int32_t thickness)
{
    const uint32_t width = static_cast<uint32_t>(thickness);
    // Thick vertical and horizontal lines are rectangles (the far edge pushed out by the thickness).
    if (width > 1) {
        if (x0 == x1) return FillCoords(colour, x0, y0, Plus(x1, thickness), y1, nullptr);
        if (y0 == y1) return FillCoords(colour, x0, y0, x1, Plus(y1, thickness), nullptr);
    }
    mContext->PushTransform();
    mContext->SetColorAt(0, NativeToArgb(colour));
    mContext->SetLineWidth(UnsignedToFloat(width));
    if (width > 1) {
        // The line's centre moved by half the extra width across its main direction.
        const float half = UnsignedToFloat(width - 1) * 0.5f;
        float points[4] = {static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1),
                           static_cast<float>(y1)};
        if (Abs(Minus(x1, x0)) >= Abs(Minus(y1, y0))) {
            points[1] += half;
            points[3] += half;
        } else {
            points[0] += half;
            points[2] += half;
        }
        mContext->DrawPrimitives(1, points, 2);
    } else {
        mContext->DrawLine(x0, y0, x1, y1);
    }
    mContext->PopTransform();
    mContext->vf41();
    return true;
}

bool cGZDrawContextAsBuffer::DrawEllipse(int32_t left, int32_t top, int32_t right, int32_t bottom, uint32_t value,
                                         int32_t)
{
    mContext->SetColorAt(0, NativeToArgb(value));
    mContext->vf37(left, top, right, bottom);
    mContext->vf41();
    return true;
}

bool cGZDrawContextAsBuffer::Blt(cIGZBuffer *source, const cGZRect *sourceRect, const cGZRect *destination,
                                 const cGZRect *clip)
{
    mContext->PushTransform();
    if (clip) mContext->IntersectClip(clip);
    SetColorFromShader(source);
    // The destination's size, placed at the source rectangle's corner.
    const int32_t dx = Minus(sourceRect->left, destination->left);
    const int32_t dy = Minus(sourceRect->top, destination->top);
    const cGZRect from{Plus(destination->left, dx), Plus(destination->top, dy), Plus(destination->right, dx),
                       Plus(destination->bottom, dy)};
    mContext->DrawTextureStretched(source, &from, destination);
    mContext->PopTransform();
    mContext->vf41();
    return true;
}

uint32_t cGZDrawContextAsBuffer::ConvertRGBValueToNative(uint8_t r, uint8_t g, uint8_t b)
{
    switch (mFormat) {
    case 4:
    case 6: return ((((r & 0xf8u) + 0x1e0u) << 5) + (g & 0xf8u)) * 4 + (b >> 3);
    case 5: return ((((r & 0xf8u) + 0xf0u) << 5) + (g & 0xfcu)) * 8 + (b >> 3);
    case 7: return (((r & 0xf0u) + 0xf00u) << 4) + (g & 0xf0u) + (b >> 4);
    case 8:
    case 9: return ((((r + 0xff00u) << 8) + g) << 8) + b;
    default: return 0;
    }
}

bool cGZDrawContextAsBuffer::ConvertNativeValueToRGB(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b)
{
    const uint32_t argb = NativeToArgb(value);
    *r = static_cast<uint8_t>(argb >> 16);
    *g = static_cast<uint8_t>(argb >> 8);
    *b = static_cast<uint8_t>(argb);
    return true;
}

uint32_t cGZDrawContextAsBuffer::ConvertRGBAValueToNative(uint8_t, uint8_t, uint8_t, uint8_t) { return 0; }

bool cGZDrawContextAsBuffer::ConvertNativeValueToRGBA(uint32_t, uint8_t *, uint8_t *, uint8_t *, uint8_t *)
{
    return false;
}

void *cGZDrawContextAsBuffer::GetColorSurfaceBits() { return mSurface.bits; }

uint32_t cGZDrawContextAsBuffer::GetColorSurfaceStride() { return static_cast<uint32_t>(mSurface.stride); }

uint32_t cGZDrawContextAsBuffer::GetChangeCounter() { return mBuffer->GetChangeCounter(); }

cIGZUnknown *cGZDrawContextAsBuffer::GetHardwareCache() { return mBuffer->GetHardwareCache(); }

bool cGZDrawContextAsBuffer::SetHardwareCache(cIGZUnknown *cache) { return mBuffer->SetHardwareCache(cache); }

bool cGZDrawContextAsBuffer::IsReady() { return true; }

uint32_t cGZDrawContextAsBuffer::NativeToArgb(uint32_t value)
{
    switch (mFormat) {
    case 4:
    case 6: {
        // 1555: each 5-bit field widened to 8 bits (its top 3 bits repeated below it).
        const uint32_t spread = ((value & 0x1f) + ((value & 0x3e0) + (value & 0x7c00) * 8) * 8) << 3;
        return ((spread >> 5) & 0x70707) + 0xff000000u + spread;
    }
    case 5: {
        // 565: red and blue widened as above, green's 6 bits with its top 2 repeated.
        const uint32_t redBlue = (((value & 0xf800) << 5) + (value & 0x1f)) << 3;
        return ((value & 0x7e0) << 5) + 0xff000000u + ((value >> 1) & 0x300) + redBlue + ((redBlue >> 5) & 0x70007);
    }
    case 7: {
        // 4444 without its alpha: each nibble doubled.
        const uint32_t nibbles = ((((value & 0xf00) << 4) + (value & 0xf0)) << 4) + (value & 0xf);
        return (nibbles << 4) + 0xff000000u + nibbles;
    }
    case 8:
    case 9: return value | 0xff000000u;
    default: return 0;
    }
}

// ---- cIGZBufferShader ----

bool cGZDrawContextAsBuffer::SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    if (mShader) mShader->SetColor(r, g, b, a);
    mColor = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | r;
    return true;
}

bool cGZDrawContextAsBuffer::GetColor(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    if (mColor == 0xffffffffu) return false;
    *r = static_cast<uint8_t>(mColor);
    *g = static_cast<uint8_t>(mColor >> 8);
    *b = static_cast<uint8_t>(mColor >> 16);
    *a = static_cast<uint8_t>(mColor >> 24);
    return true;
}

// ---- cIGZDrawContextAsBuffer ----

cIGZBuffer *cGZDrawContextAsBuffer::GetBuffer() { return static_cast<cIGZBuffer *>(this); }

cIGZDrawContext *cGZDrawContextAsBuffer::GetContext()
{
    if (!mContext->HasTarget()) {
        mContext->SetTarget(mBuffer);
        mContext->BeginBatch();
    }
    return mContext;
}

cIGZDrawContext *cGZDrawContextAsBuffer::LockContext()
{
    Lock(kLockWrite | 0x10);
    return mContext;
}

void cGZDrawContextAsBuffer::UnlockContext() { Unlock(kLockWrite | 0x10); }

void cGZDrawContextAsBuffer::Init(cIGZDrawContext *context, cIGZBuffer *target)
{
    if (context != mContext) {
        cIGZDrawContext *const old = mContext;
        if (context) context->AddRef();
        mContext = context;
        if (old) old->Release();
    }
    if (target != mBuffer) {
        cIGZBuffer *const old = mBuffer;
        if (target) target->AddRef();
        mBuffer = target;
        if (old) old->Release();
    }
    mBounds = *target->GetBoundsPtr();
    mFormat = target->GetNativeColorType().format;
    mSurface.bytesPerPixel = target->GetBytesPerPixel();
    mSurface.width = static_cast<uint32_t>(Minus(mBounds.right, mBounds.left));
    mSurface.height = static_cast<uint32_t>(Minus(mBounds.bottom, mBounds.top));
    mContext->SetTarget(nullptr);
    mColor = 0xffffffffu;
    if (mShader) {
        cIGZBufferShader *const old = mShader;
        mShader = nullptr;
        old->Release();
    }
    uint8_t r, g, b, a;
    if (mBuffer->QueryInterface(cIGZBufferShader::kIid, reinterpret_cast<void **>(&mShader)) &&
        mShader->GetColor(&r, &g, &b, &a))
        mColor = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) |
                 r;
}

void cGZDrawContextAsBuffer::Refresh()
{
    mBounds = *mBuffer->GetBoundsPtr();
    mContext->SetTarget(nullptr);
    mContext->SetTarget(mBuffer);
    mContext->BeginBatch();
}

void cGZDrawContextAsBuffer::SetColorFromShader(cIGZUnknown *source)
{
    cIGZBufferShader *shader = nullptr;
    uint8_t r, g, b, a;
    if (source->QueryInterface(cIGZBufferShader::kIid, reinterpret_cast<void **>(&shader)) &&
        shader->GetColor(&r, &g, &b, &a))
        mContext->SetColor((static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
                           (static_cast<uint32_t>(g) << 8) | b);
    else
        mContext->SetColor(0xffffffffu);
    if (shader) shader->Release();
}
