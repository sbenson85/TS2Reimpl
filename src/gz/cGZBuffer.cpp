#include "gz/cGZBuffer.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GZConvertPixels.h"
#include "gz/cRZCursor.h" // GetGraphicSystem
#include "rz/RZFastFixedAllocators.h"

#include <cstring>
#include <new>

namespace {

// Differences and sums of coordinates wrap as the original's 32-bit sub / add do.
int32_t Minus(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }
int32_t Plus(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); }
int32_t Times(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)); }
int32_t Abs(int32_t a) { return a < 0 ? Minus(0, a) : a; }

// ClipLine's outcode: 1 above the rectangle, 2 below, 4 left of it, 8 right of it.
uint32_t OutCode(int32_t x, int32_t y, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    uint32_t code = x < left ? 4 : 0;
    if (x > right) code += 8;
    if (y < top) code += 1;
    if (y > bottom) code += 2;
    return code;
}

// Row fills of FillRect: `count` 16-bit / 32-bit copies of `value` from `row` on (the image's helpers 0x1b2ccf and
// 0x1b2d0a do the same with rep stosd).
void FillWords(uint16_t *row, uint16_t value, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) row[i] = value;
}
void FillDwords(uint32_t *row, uint32_t value, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) row[i] = value;
}

// The graphics service's interface the display mode comes from, and what its slot 15 fills (five dwords; the first is a
// bit field whose low six bits are the colour depth). The original builds the request with bit 6 set, bits 0..5, 7 and 8
// clear, and leaves bits 9..31 as the stack had them (`and eax, 0xfffffe40; or eax, 0x40`); ours clears them.
constexpr uint32_t kDisplayDriverIid = 0xc47b747c;
struct DisplayModeRequest {
    uint32_t bits;
    uint32_t words[4];
};

} // namespace

// ---- cGZColorType ----

cGZColorType::cGZColorType(uint32_t format) : format(format)
{
    switch (format) {
    case 2: bits = 8; break;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7: bits = 16; break;
    case 8:
    case 9: bits = 32; break;
    default: bits = 0; break;
    }
}

// ---- cGZBuffer: construction ----

cGZBuffer::cGZBuffer()
    : mReady(false), mColorType(3, 16), mBounds{0, 0, 0, 0}, mTransparentColour(0), mFlags(0), mRefCount(0),
      mChangeCounter(0), mShaderColor(0xffffffffu), mLockCount(0), mBytesPerPixel(0), mPixels(nullptr), mStride(0),
      mLockFlags(0), mHardwareCache(nullptr), mOuter(nullptr)
{
}

cGZBuffer::~cGZBuffer()
{
    delete[] mPixels;
    mPixels = nullptr;
    mReady = false;
    if (mHardwareCache) mHardwareCache->Release();
}

void *cGZBuffer::operator new(size_t size)
{
    if (size != kSize) return ::operator new(size);
    return reinterpret_cast<RZFixedAllocatorBase *>(&ts2_rva_105f82c)->Alloc();
}

void cGZBuffer::operator delete(void *block, size_t size)
{
    if (size != kSize) {
        ::operator delete(block);
        return;
    }
    reinterpret_cast<RZFastFixed0x54 *>(&ts2_rva_105f82c)->Free(block);
}

cIGZBuffer *cGZBuffer::Create()
{
    return new cGZBuffer;
}

// ---- cGZBuffer: cIGZUnknown ----

bool cGZBuffer::QueryInterface(uint32_t riid, void **out)
{
    if (mOuter) return mOuter->QueryInterface(riid, out);
    return NonDelegatingQueryInterface(riid, out);
}

uint32_t cGZBuffer::AddRef()
{
    if (mOuter) return mOuter->AddRef();
    return ++mRefCount;
}

uint32_t cGZBuffer::Release()
{
    if (mOuter) return mOuter->Release();
    if (mRefCount != 1) return --mRefCount;
    mRefCount = 0;
    delete this;
    return 0;
}

// ---- cGZBuffer: cIGZBuffer ----

bool cGZBuffer::Init(uint32_t width, uint32_t height, uint32_t format, uint32_t bits)
{
    if (mReady || width == 0 || height == 0) return false;
    if (static_cast<int32_t>(width | height) < 0) return false;
    mBounds.right = static_cast<int32_t>(width);
    mBounds.left = 0;
    mBounds.top = 0;
    mBounds.bottom = static_cast<int32_t>(height);
    mColorType.format = format;
    mColorType.bits = bits;
    // The fields stay written when the allocation fails; the ready flag does not get set.
    if (!AllocatePixels()) return false;
    mReady = true;
    return true;
}

bool cGZBuffer::Shutdown()
{
    FreePixelMemory();
    mReady = false;
    return true;
}

bool cGZBuffer::Uninitialize()
{
    FreePixelMemory();
    mReady = false;
    return true;
}

bool cGZBuffer::Lock(uint32_t flags)
{
    if (!mPixels) return false;
    if (flags & kLockCountsChange) ++mChangeCounter;
    mLockFlags |= flags;
    ++mLockCount;
    return true;
}

bool cGZBuffer::Unlock(uint32_t flags)
{
    mLockFlags &= ~flags;
    --mLockCount; // 16-bit: zero wraps to 0xffff, as the original's add 0xffff does
    return true;
}

bool cGZBuffer::IsLocked()
{
    return mLockCount != 0;
}

uint32_t cGZBuffer::GetWidth()
{
    return static_cast<uint32_t>(mBounds.right);
}

uint32_t cGZBuffer::GetHeight()
{
    return static_cast<uint32_t>(mBounds.bottom);
}

cGZRect cGZBuffer::GetBounds()
{
    return mBounds;
}

const cGZRect *cGZBuffer::GetBoundsPtr()
{
    return &mBounds;
}

uint32_t cGZBuffer::GetMemoryUsage()
{
    if (!mPixels) return 0;
    // Row length in pixels rounded up to a multiple of 4, times bytes per pixel, times the height.
    const uint32_t alignedWidth = ((static_cast<uint32_t>(mBounds.right) - 1) | 3) + 1;
    return alignedWidth * (mColorType.bits >> 3) * static_cast<uint32_t>(mBounds.bottom);
}

cGZColorType cGZBuffer::GetColorType()
{
    return mColorType;
}

cGZColorType cGZBuffer::GetNativeColorType()
{
    return mColorType;
}

uint32_t cGZBuffer::GetBitsPerPixel()
{
    return mColorType.bits;
}

uint32_t cGZBuffer::GetBytesPerPixel()
{
    return mColorType.bits >> 3;
}

bool cGZBuffer::Fill(uint32_t value, const cGZRect *clip)
{
    return FillRect(value, GetBoundsPtr(), clip);
}

bool cGZBuffer::FillCoords(uint32_t value, int32_t left, int32_t top, int32_t right, int32_t bottom, const cGZRect *clip)
{
    const cGZRect rect{left, top, right, bottom};
    return FillRect(value, &rect, clip);
}

bool cGZBuffer::FillRect(uint32_t value, const cGZRect *rect, const cGZRect *clip)
{
    // The rectangle's own extent is the "source" ClipRects moves along with the destination; only the destination is used.
    cGZRect extent{0, 0, Minus(rect->right, rect->left), Minus(rect->bottom, rect->top)};
    cGZRect target = *rect;
    if (!ClipRects(this, &extent, &target, clip)) return false;
    const uint32_t count = static_cast<uint32_t>(Minus(target.right, target.left));
    const uint32_t top = static_cast<uint32_t>(target.top);
    const uint32_t bottom = static_cast<uint32_t>(target.bottom);
    const uint32_t left = static_cast<uint32_t>(target.left);
    switch (mColorType.bits) {
    case 16: {
        uint8_t *pixels = mPixels;
        const uint32_t pitch = GetColorSurfaceStride() >> 1; // in pixels
        uint16_t *row = reinterpret_cast<uint16_t *>(pixels) + (top * pitch + left);
        for (uint32_t y = top; y < bottom; ++y, row += pitch) FillWords(row, static_cast<uint16_t>(value), count);
        break;
    }
    case 8: {
        uint8_t *pixels = mPixels;
        const uint32_t stride = GetColorSurfaceStride();
        uint8_t *row = pixels + (stride * top + left);
        for (uint32_t y = top; y < bottom; ++y, row += stride) std::memset(row, static_cast<uint8_t>(value), count);
        break;
    }
    case 32: {
        uint8_t *pixels = mPixels;
        const uint32_t pitch = GetColorSurfaceStride() >> 2; // in pixels
        uint32_t *row = reinterpret_cast<uint32_t *>(pixels) + (top * pitch + left);
        for (uint32_t y = top; y < bottom; ++y, row += pitch) FillDwords(row, value, count);
        break;
    }
    default: break; // 24 bits and others: nothing is written, but the buffer counts as changed
    }
    ++mChangeCounter;
    return true;
}

uint32_t cGZBuffer::GetPixel(uint32_t x, uint32_t y)
{
    // Pixel bytes are read one at a time, low byte first; the size selector is bytes per pixel minus one, unsigned, so zero
    // wraps to the default case (one byte).
    const uint8_t *p = mPixels + (mStride * y + static_cast<uint32_t>(mBytesPerPixel) * x);
    switch (static_cast<uint32_t>(mBytesPerPixel) - 1u) {
    case 0: return p[0];
    case 1: return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8);
    case 2: return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16);
    case 3:
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
               (static_cast<uint32_t>(p[3]) << 24);
    default: return p[0];
    }
}

void cGZBuffer::SetPixel(uint32_t x, uint32_t y, uint32_t value)
{
    uint8_t *p = mPixels + (mStride * y + static_cast<uint32_t>(mBytesPerPixel) * x);
    switch (static_cast<uint32_t>(mBytesPerPixel) - 1u) {
    case 3:
        p[3] = static_cast<uint8_t>(value >> 24);
        [[fallthrough]];
    case 2:
        p[2] = static_cast<uint8_t>(value >> 16);
        [[fallthrough]];
    case 1:
        p[1] = static_cast<uint8_t>(value >> 8);
        [[fallthrough]];
    default:
        p[0] = static_cast<uint8_t>(value);
        break;
    }
}

void cGZBuffer::SetTransparency(uint32_t colour)
{
    mFlags |= kFlagTransparent;
    mTransparentColour = colour;
    // The cache's slot 9 takes no arguments: the colour is not passed on.
    if (mHardwareCache) VirtualCall<void(__thiscall *)(cIGZUnknown *), 9>(mHardwareCache);
}

bool cGZBuffer::GetTransparency()
{
    return ((mFlags >> 1) & 1) != 0;
}

bool cGZBuffer::IsPixelTransparent(uint32_t x, uint32_t y)
{
    if (!(mFlags & kFlagTransparent)) return false;
    if (x >= static_cast<uint32_t>(mBounds.right) || y >= static_cast<uint32_t>(mBounds.bottom)) return false;
    const uint32_t mask = FormatMask(mColorType.format);
    const uint32_t pixel = GetPixel(x, y);
    return (pixel & mask) == (mTransparentColour & mask);
}

bool cGZBuffer::GetTransparentColor(uint32_t *colour)
{
    if (!(mFlags & kFlagTransparent)) return false;
    *colour = mTransparentColour;
    return true;
}

void cGZBuffer::RemoveTransparency()
{
    mFlags &= ~kFlagTransparent;
    if (mHardwareCache) VirtualCall<void(__thiscall *)(cIGZUnknown *), 9>(mHardwareCache);
}

bool cGZBuffer::DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t colour, int32_t thickness)
{
    const uint16_t colour16 = static_cast<uint16_t>(colour);
    uint8_t *const bits = static_cast<uint8_t *>(GetColorSurfaceBits());
    const int32_t pitch = static_cast<int32_t>(GetColorSurfaceStride()) >> 1; // in 16-bit pixels (the thick lines')
    const int32_t width = mBounds.right;
    const int32_t height = mBounds.bottom;
    // The line's pixels are the top-left corners of thickness-wide squares: the ends are clipped so the squares fit.
    const int32_t lastX = Minus(width, thickness);
    const int32_t lastY = Minus(height, thickness);
    if (!ClipLine(&x0, &y0, &x1, &y1, 0, 0, lastX, lastY)) return true;

    // Horizontal and vertical lines are filled rectangles (FillCoords' result is not looked at).
    if (y0 == y1) {
        if (x0 > x1) FillCoords(colour, x1, y0, Plus(x0, 1), Plus(y0, thickness), nullptr);
        else FillCoords(colour, x0, y0, Plus(x1, 1), Plus(y0, thickness), nullptr);
        return true;
    }
    if (x0 == x1) {
        if (y0 > y1) FillCoords(colour, x0, y1, Plus(x0, thickness), Plus(y0, 1), nullptr);
        else FillCoords(colour, x0, y0, Plus(x0, thickness), Plus(y1, 1), nullptr);
        return true;
    }
    if (!bits) return true;

    // Bresenham: one step along the major axis per pixel; the minor axis steps too while the error is not negative.
    const int32_t dx = Abs(Minus(x1, x0));
    const int32_t dy = Abs(Minus(y1, y0));
    int32_t minorX, minorY, minorDelta, majorDelta, error, count;
    if (dx >= dy) {
        minorX = 1;
        minorY = 0;
        minorDelta = Plus(dy, dy);
        error = Minus(minorDelta, dx);
        majorDelta = Plus(Minus(dy, dx), Minus(dy, dx));
        count = Plus(dx, 1);
    } else {
        minorX = 0;
        minorY = 1;
        minorDelta = Plus(dx, dx);
        error = Minus(minorDelta, dy);
        majorDelta = Plus(Minus(dx, dy), Minus(dx, dy));
        count = Plus(dy, 1);
    }
    int32_t stepX = 1, stepY = 1;
    if (x0 > x1) minorX = -minorX, stepX = -1;
    if (y0 > y1) minorY = -minorY, stepY = -1;

    int32_t x = x0, y = y0;
    if (thickness == 1) {
        // One pixel per step, at the buffer's own depth (by bits per pixel; any other depth draws nothing).
        const uint32_t bitsPerPixel = GetBitsPerPixel();
        if (bitsPerPixel != 8 && bitsPerPixel != 16 && bitsPerPixel != 24 && bitsPerPixel != 32) return true;
        const uint32_t stride = GetColorSurfaceStride();
        uintptr_t row = reinterpret_cast<uintptr_t>(bits) + stride * static_cast<uint32_t>(y0);
        for (; count >= 1; --count) {
            const uintptr_t at = row + static_cast<uint32_t>(x) * (bitsPerPixel >> 3);
            switch (bitsPerPixel) {
            case 8: *reinterpret_cast<uint8_t *>(at) = static_cast<uint8_t>(colour); break;
            case 16: *reinterpret_cast<uint16_t *>(at) = colour16; break;
            case 24:
                reinterpret_cast<uint8_t *>(at)[0] = static_cast<uint8_t>(colour);
                reinterpret_cast<uint8_t *>(at)[1] = static_cast<uint8_t>(colour >> 8);
                reinterpret_cast<uint8_t *>(at)[2] = static_cast<uint8_t>(colour >> 16);
                break;
            default: *reinterpret_cast<uint32_t *>(at) = colour; break;
            }
            if (error < 0) {
                row += stride * static_cast<uint32_t>(minorY);
                x = Plus(x, minorX);
                error = Plus(error, minorDelta);
            } else {
                row += stride * static_cast<uint32_t>(stepY);
                x = Plus(x, stepX);
                error = Plus(error, majorDelta);
            }
        }
        return true;
    }

    // Thick lines: a thickness x thickness square of 16-bit pixels per step, whatever the depth, growing from the point
    // against the line's direction on each axis (left of it when x falls, above it when y falls).
    if (count == 0) return true;
    const uintptr_t base = reinterpret_cast<uintptr_t>(bits);
    const uint32_t rowBytes = static_cast<uint32_t>(pitch) * 2;
    const uint16_t rows = static_cast<uint16_t>(thickness);
    // The end of the memory the checked squares stay before.
    const uint32_t end = static_cast<uint32_t>(base) + static_cast<uint32_t>(height) * static_cast<uint32_t>(pitch) * 2;
    const int32_t margin = Minus(thickness, 1);
    // Once a square needs the checks, every later one gets them too (the original's loop stays in that branch). The clip
    // keeps every point inside [0, lastX] x [0, lastY], so only x or y below the margin starts the checks, the right-edge
    // cuts below never apply, and a checked square fully inside writes what an unchecked one would.
    bool checked = false;
    do {
        if (checked || x < margin || x > lastX || y < margin || y > lastY) {
            // Near an edge: each row is checked against the memory, and cut at the buffer's left or right edge.
            checked = true;
            int32_t start;
            if (stepX == -1) {
                start = Plus(Minus(x, thickness), 1);
                if (start < 0) start = 0;
            } else {
                start = Minus(width, 1) < x ? Minus(width, 1) : x;
            }
            uint32_t at = static_cast<uint32_t>(base) +
                          2 * (static_cast<uint32_t>(y) * static_cast<uint32_t>(pitch) + static_cast<uint32_t>(start));
            for (uint16_t r = rows; r != 0; --r) {
                if (at >= static_cast<uint32_t>(base) && at < end) {
                    uint32_t n;
                    if (stepX == -1) n = x < margin ? static_cast<uint32_t>(Plus(x, 1)) : static_cast<uint32_t>(thickness);
                    else n = x <= lastX ? static_cast<uint32_t>(thickness) : static_cast<uint32_t>(Minus(width, x));
                    FillWords(reinterpret_cast<uint16_t *>(static_cast<uintptr_t>(at)), colour16, n);
                }
                at = stepY != -1 ? at + rowBytes : at - rowBytes;
            }
        } else {
            uint32_t at = static_cast<uint32_t>(base) +
                          2 * (static_cast<uint32_t>(y) * static_cast<uint32_t>(pitch) + static_cast<uint32_t>(x));
            if (stepX == -1) at -= 2 * (static_cast<uint32_t>(thickness) - 1);
            for (uint16_t r = rows; r != 0; --r) {
                FillWords(reinterpret_cast<uint16_t *>(static_cast<uintptr_t>(at)), colour16, static_cast<uint32_t>(thickness));
                at = stepY != -1 ? at + rowBytes : at - rowBytes;
            }
        }
        if (error < 0) {
            x = Plus(x, minorX);
            y = Plus(y, minorY);
            error = Plus(error, minorDelta);
        } else {
            x = Plus(x, stepX);
            y = Plus(y, stepY);
            error = Plus(error, majorDelta);
        }
    } while (--count != 0);
    return true;
}

bool cGZBuffer::Blt(cIGZBuffer *source, const cGZRect *sourceRect, const cGZRect *destination, const cGZRect *clip)
{
    // Only the destination's left / top count: its right / bottom become the source's extent from there.
    cGZRect from = *sourceRect;
    cGZRect to = *destination;
    to.right = Plus(Minus(from.right, from.left), to.left);
    to.bottom = Plus(Minus(from.bottom, from.top), to.top);
    if (!ClipRects(this, &from, &to, clip)) return false;
    return BltUnclipped(source, &from, &to);
}

uint32_t cGZBuffer::ConvertRGBValueToNative(uint8_t r, uint8_t g, uint8_t b)
{
    switch (mColorType.format) {
    case 4:
    case 6: return 0x8000u | ((r & 0xf8u) << 7) | ((g & 0xf8u) << 2) | (b >> 3u);      // 1555, alpha set
    case 5: return ((r & 0xf8u) << 8) | ((g & 0xfcu) << 3) | (b >> 3u);                // 565
    case 7: return 0xf000u | ((r & 0xf0u) << 4) | (g & 0xf0u) | (b >> 4u);            // 4444, alpha set
    case 8:
    case 9: return 0xff000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b; // 8888
    default: return 0;
    }
}

bool cGZBuffer::ConvertNativeValueToRGB(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b)
{
    // Each channel is widened to 8 bits by repeating its top bits (the multiply by 33 / 65 / 17 and shift).
    switch (mColorType.format) {
    case 4:
    case 6:
        *r = static_cast<uint8_t>(((value & 0x7c00u) * 33u) >> 12);
        *g = static_cast<uint8_t>(((value & 0x3e0u) * 33u) >> 7);
        *b = static_cast<uint8_t>(((value & 0x1fu) * 33u) >> 2);
        return true;
    case 5:
        *r = static_cast<uint8_t>(((value & 0xf800u) * 33u) >> 13);
        *g = static_cast<uint8_t>(((value & 0x7e0u) * 65u) >> 9);
        *b = static_cast<uint8_t>(((value & 0x1fu) * 33u) >> 2);
        return true;
    case 7:
        *r = static_cast<uint8_t>(((value & 0xf00u) * 17u) >> 8);
        *g = static_cast<uint8_t>(((value & 0xf0u) * 17u) >> 4);
        *b = static_cast<uint8_t>((value & 0xfu) * 17u);
        return true;
    case 8:
    case 9:
        *r = static_cast<uint8_t>(value >> 16);
        *g = static_cast<uint8_t>(value >> 8);
        *b = static_cast<uint8_t>(value);
        return true;
    default: return false;
    }
}

uint32_t cGZBuffer::ConvertRGBAValueToNative(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    switch (mColorType.format) {
    case 4:
    case 6: return ((a & 0x80u) << 8) | ((r & 0xf8u) << 7) | ((g & 0xf8u) << 2) | (b >> 3u); // 1555
    case 5: return ((r & 0xf8u) << 8) | ((g & 0xfcu) << 3) | (b >> 3u);                     // 565, no alpha
    case 7: return ((a & 0xf0u) << 8) | ((r & 0xf0u) << 4) | (g & 0xf0u) | (b >> 4u);       // 4444
    case 8:
    case 9:
        return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
    default: return 0;
    }
}

bool cGZBuffer::ConvertNativeValueToRGBA(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    switch (mColorType.format) {
    case 4:
    case 6:
        // The alpha bit widened like a 5-bit channel: 0x84 when set (the original's arithmetic, not 0xff).
        *a = static_cast<uint8_t>(((value & 0x8000u) * 33u) >> 13);
        *r = static_cast<uint8_t>(((value & 0x7c00u) * 33u) >> 12);
        *g = static_cast<uint8_t>(((value & 0x3e0u) * 33u) >> 7);
        *b = static_cast<uint8_t>(((value & 0x1fu) * 33u) >> 2);
        return true;
    case 5:
        *a = 0xff;
        *r = static_cast<uint8_t>(((value & 0xf800u) * 33u) >> 13);
        *g = static_cast<uint8_t>(((value & 0x7e0u) * 65u) >> 9);
        *b = static_cast<uint8_t>(((value & 0x1fu) * 33u) >> 2);
        return true;
    case 7: {
        // The alpha nibble lands in the top four bits only (the widened byte shifted left by four), 0xf0 becoming 0xff.
        const uint8_t alpha = static_cast<uint8_t>(static_cast<uint8_t>(((value & 0xf000u) * 17u) >> 12) << 4);
        *a = alpha;
        if (alpha == 0xf0) *a = 0xff;
        *r = static_cast<uint8_t>(((value & 0xf00u) * 17u) >> 8);
        *g = static_cast<uint8_t>(((value & 0xf0u) * 17u) >> 4);
        *b = static_cast<uint8_t>((value & 0xfu) * 17u);
        return true;
    }
    case 8:
    case 9:
        *a = static_cast<uint8_t>(value >> 24);
        *r = static_cast<uint8_t>(value >> 16);
        *g = static_cast<uint8_t>(value >> 8);
        *b = static_cast<uint8_t>(value);
        return true;
    default: return false;
    }
}

void *cGZBuffer::GetColorSurfaceBits()
{
    return mPixels;
}

uint32_t cGZBuffer::GetColorSurfaceStride()
{
    return mStride;
}

uint32_t cGZBuffer::GetChangeCounter()
{
    return mChangeCounter;
}

cIGZUnknown *cGZBuffer::GetHardwareCache()
{
    return mHardwareCache;
}

bool cGZBuffer::SetHardwareCache(cIGZUnknown *cache)
{
    if (mHardwareCache) {
        cIGZUnknown *old = mHardwareCache;
        mHardwareCache = nullptr;
        old->Release();
    }
    // The cache's slot 3 takes the buffer (and may install a cache of its own: the current one is read after it).
    if (!VirtualCall<bool(__thiscall *)(cIGZUnknown *, cIGZBuffer *), 3>(cache, this)) return false;
    cIGZUnknown *current = mHardwareCache;
    if (cache != current) {
        cache->AddRef();
        mHardwareCache = cache;
        if (current) current->Release();
    }
    return true;
}

bool cGZBuffer::IsReady()
{
    return mReady;
}

// ---- cGZBuffer: the slots it adds ----

bool cGZBuffer::ClipRects(cIGZBuffer *bounds, cGZRect *source, cGZRect *destination, const cGZRect *clip)
{
    cGZRect &to = *destination;
    cGZRect &from = *source;
    // Against the bounds' rectangle (right / bottom from slot 12, left / top 0); each step gives up as soon as it empties
    // the destination.
    int32_t over = Minus(to.right, bounds->GetBoundsPtr()->right);
    if (over > 0) {
        to.right = Minus(to.right, over);
        from.right = Minus(from.right, over);
        if (to.right <= to.left) return false;
    }
    over = Minus(0, to.left);
    if (over > 0) {
        to.left = 0;
        from.left = Plus(from.left, over);
        if (to.left >= to.right) return false;
    }
    over = Minus(to.bottom, bounds->GetBoundsPtr()->bottom);
    if (over > 0) {
        to.bottom = Minus(to.bottom, over);
        from.bottom = Minus(from.bottom, over);
        if (to.bottom <= to.top) return false;
    }
    over = Minus(0, to.top);
    if (over > 0) {
        to.top = 0;
        from.top = Plus(from.top, over);
        if (to.top >= to.bottom) return false;
    }
    // Against the clip rectangle, all four edges before one check.
    // ORIGINAL-BUG: the edges are compared by a wrapping difference, so an edge far enough from the clip's (more than 2^31
    // apart) takes the clip's edge even when that lies outside the buffer, and the fill or blit then reaches past it.
    if (clip) {
        over = Minus(to.right, clip->right);
        if (over > 0) {
            to.right = clip->right;
            from.right = Minus(from.right, over);
        }
        over = Minus(clip->left, to.left);
        if (over > 0) {
            to.left = clip->left;
            from.left = Plus(from.left, over);
        }
        over = Minus(to.bottom, clip->bottom);
        if (over > 0) {
            to.bottom = clip->bottom;
            from.bottom = Minus(from.bottom, over);
        }
        over = Minus(clip->top, to.top);
        if (over > 0) {
            to.top = clip->top;
            from.top = Plus(from.top, over);
        }
    }
    if (to.left >= to.right) return false;
    return to.top < to.bottom;
}

bool cGZBuffer::AllocatePixels()
{
    if (AllocatePixelMemory()) return true;
    mPixels = nullptr;
    return false;
}

bool cGZBuffer::AllocatePixelMemory()
{
    InitPixelFormat();
    const uint32_t bytes = mColorType.bits >> 3;
    const uint32_t stride = (static_cast<uint32_t>(mBounds.right) * bytes + 3) & ~3u;
    mPixels = new uint8_t[stride * static_cast<uint32_t>(mBounds.bottom)];
    ++mChangeCounter;
    mStride = stride;
    mBytesPerPixel = static_cast<uint8_t>(bytes);
    return true;
}

bool cGZBuffer::FreePixelMemory()
{
    delete[] mPixels;
    mPixels = nullptr;
    return true;
}

void cGZBuffer::InitPixelFormat()
{
    if (mColorType.format != 3 && mColorType.format != 1) return;
    // The graphics service, cached by the getter (not counted: nothing is released).
    cIGZUnknown *graphics = reinterpret_cast<cIGZUnknown *>(GetGraphicSystem()); // 0x9c5a05, ours
    if (!graphics) {
        mColorType.format = 5;
        mColorType.bits = 16;
        return;
    }
    cIGZUnknown *driver = nullptr;
    if (graphics->QueryInterface(kDisplayDriverIid, reinterpret_cast<void **>(&driver))) {
        DisplayModeRequest mode{0x40, {0, 0, 0, 0}};
        VirtualCall<void(__thiscall *)(cIGZUnknown *, DisplayModeRequest *), 15>(driver, &mode);
        const uint32_t depth = mode.bits & 0x3f;
        mColorType.bits = depth > 16 ? 32 : 16;
        mColorType.format = depth > 16 ? 9 : 7;
        driver->Release();
        return;
    }
    // No display driver: the service's own colour type (slot 9, written through the result pointer it takes).
    cGZColorType type;
    VirtualCall<cGZColorType *(__thiscall *)(cIGZUnknown *, cGZColorType *), 9>(graphics, &type);
    mColorType = type;
}

bool cGZBuffer::BltUnclipped(cIGZBuffer *source, const cGZRect *sourceRect, const cGZRect *destination)
{
    if (!source->Lock(kLockRead)) return true;
    const int32_t width = Minus(destination->right, destination->left);
    if (width != 0) {
        const int32_t height = Minus(destination->bottom, destination->top);
        if (height != 0) {
            uint8_t *targetBits = static_cast<uint8_t *>(GetColorSurfaceBits());
            const uint8_t *sourceBits = static_cast<const uint8_t *>(source->GetColorSurfaceBits());
            if (targetBits && sourceBits) {
                const cGZColorType sourceType = source->GetNativeColorType();
                const uint32_t sourceStride = source->GetColorSurfaceStride();
                const uint32_t targetStride = GetColorSurfaceStride();
                uint8_t *to = targetBits + (static_cast<uint32_t>(destination->top) * targetStride +
                                            BytesPerPixelOf(mColorType.format) * static_cast<uint32_t>(destination->left));
                const uint8_t *from =
                    sourceBits + (static_cast<uint32_t>(sourceRect->top) * sourceStride +
                                  BytesPerPixelOf(sourceType.format) * static_cast<uint32_t>(sourceRect->left));
                ++mChangeCounter;
                uint32_t key;
                const bool keyed = source->GetTransparentColor(&key);
                GZConvertPixels(to, static_cast<int32_t>(targetStride), mColorType.format, from,
                                static_cast<int32_t>(sourceStride), sourceType.format, width, height, keyed ? &key : nullptr);
            }
        }
    }
    source->Unlock(kLockRead);
    return true;
}

// ---- cGZBuffer: cIGZBufferShader ----

bool cGZBuffer::SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    mShaderColor = static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) |
                   (static_cast<uint32_t>(a) << 24);
    return true;
}

bool cGZBuffer::GetColor(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
    if (mShaderColor == 0xffffffffu) return false;
    *r = static_cast<uint8_t>(mShaderColor);
    *g = static_cast<uint8_t>(mShaderColor >> 8);
    *b = static_cast<uint8_t>(mShaderColor >> 16);
    *a = static_cast<uint8_t>(mShaderColor >> 24);
    return true;
}

// ---- cGZBuffer: cIGZAggregate ----

bool cGZBuffer::NonDelegatingQueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case 1:
    case cIGZBuffer::kIid: *out = static_cast<cIGZBuffer *>(this); break;
    case cIGZAggregate::kIid: *out = static_cast<cIGZAggregate *>(this); break;
    case cIGZBufferShader::kIid: *out = static_cast<cIGZBufferShader *>(this); break;
    default: return false;
    }
    AddRef();
    return true;
}

void cGZBuffer::SetOuter(cIGZUnknown *outer)
{
    mOuter = outer;
}

cIGZUnknown *cGZBuffer::GetOuter()
{
    return mOuter;
}

// ---- cGZBuffer: helpers ----

uint32_t cGZBuffer::FormatMask(uint32_t format)
{
    switch (format) {
    case 4:
    case 6: return 0x7fff;
    case 5: return 0xffff;
    case 7: return 0xfff;
    case 8:
    case 9: return 0xffffff;
    default: return 0xffffffffu;
    }
}

uint32_t cGZBuffer::BytesPerPixelOf(uint32_t format)
{
    switch (format) {
    case 4:
    case 5:
    case 6:
    case 7: return 2;
    case 8:
    case 9: return 4;
    default: return 0;
    }
}

bool cGZBuffer::ClipLine(int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1, int32_t left, int32_t top, int32_t right,
                         int32_t bottom)
{
    uint32_t code0 = OutCode(*x0, *y0, left, top, right, bottom);
    uint32_t code1 = OutCode(*x1, *y1, left, top, right, bottom);
    for (;;) {
        if (!(code0 | code1)) return true;
        if (code0 & code1) return false;
        // Move the end that is outside (the first, if both are) onto the edge it is past: top, bottom, right, left.
        const uint32_t out = code0 ? code0 : code1;
        int32_t x, y;
        if (out & 1) {
            x = Plus(*x0, Times(Minus(*x1, *x0), Minus(top, *y0)) / Minus(*y1, *y0));
            y = top;
        } else if (out & 2) {
            x = Plus(*x0, Times(Minus(*x1, *x0), Minus(bottom, *y0)) / Minus(*y1, *y0));
            y = bottom;
        } else if (out & 8) {
            y = Plus(*y0, Times(Minus(*y1, *y0), Minus(right, *x0)) / Minus(*x1, *x0));
            x = right;
        } else {
            y = Plus(*y0, Times(Minus(*y1, *y0), Minus(left, *x0)) / Minus(*x1, *x0));
            x = left;
        }
        if (out == code0) {
            *x0 = x;
            *y0 = y;
            code0 = OutCode(x, y, left, top, right, bottom);
        } else {
            *x1 = x;
            *y1 = y;
            code1 = OutCode(x, y, left, top, right, bottom);
        }
    }
}

// ---- cGZTransientBuffer ----

cGZTransientBuffer::cGZTransientBuffer() : mTarget(nullptr), mMemoryOwner(nullptr), mLockTotal(0), mExternalMemory(false)
{
}

cGZTransientBuffer::~cGZTransientBuffer()
{
    if (mExternalMemory && mReady && mTarget) mTarget->Commit(this);
    if (mTarget) mTarget->Detach(this);
    if (mMemoryOwner) mMemoryOwner->Release();
    if (mTarget) mTarget->Release();
    // ~cGZBuffer then frees the pixel pointer, external memory or not (as the original's 0x8c7950 does).
}

cIGZTransientBuffer *cGZTransientBuffer::Create()
{
    return new cGZTransientBuffer;
}

bool cGZTransientBuffer::QueryInterface(uint32_t riid, void **out)
{
    // No outer unknown here: always the object's own answer.
    return NonDelegatingQueryInterface(riid, out);
}

uint32_t cGZTransientBuffer::AddRef()
{
    return cGZBuffer::AddRef();
}

uint32_t cGZTransientBuffer::Release()
{
    return cGZBuffer::Release();
}

bool cGZTransientBuffer::Lock(uint32_t flags)
{
    if (!mReady && mTarget) {
        Realize();
        if (!mReady) return false;
    }
    ++mLockTotal;
    return cGZBuffer::Lock(flags);
}

bool cGZTransientBuffer::AllocatePixelMemory()
{
    cGZBuffer::AllocatePixelMemory();
    mExternalMemory = false;
    return true;
}

bool cGZTransientBuffer::FreePixelMemory()
{
    if (!mExternalMemory) return cGZBuffer::FreePixelMemory();
    mExternalMemory = false;
    if (mMemoryOwner) {
        cIGZUnknown *owner = mMemoryOwner;
        mMemoryOwner = nullptr;
        owner->Release();
    }
    mReady = false;
    mPixels = nullptr;
    return true;
}

bool cGZTransientBuffer::NonDelegatingQueryInterface(uint32_t riid, void **out)
{
    if (riid == cIGZTransientBuffer::kIid) {
        *out = static_cast<cIGZTransientBuffer *>(this);
        AddRef();
        return true;
    }
    return cGZBuffer::NonDelegatingQueryInterface(riid, out);
}

cIGZBuffer *cGZTransientBuffer::GetBuffer()
{
    return this;
}

void cGZTransientBuffer::SetMemory(void *pixels, uint32_t stride, uint32_t width, uint32_t height, uint32_t format,
                                   cIGZUnknown *owner)
{
    if (mPixels && !mExternalMemory) FreePixelMemory();
    mStride = stride;
    mBounds.right = static_cast<int32_t>(width);
    mPixels = static_cast<uint8_t *>(pixels);
    mBounds.left = 0;
    mBounds.top = 0;
    mBounds.bottom = static_cast<int32_t>(height);
    mColorType = cGZColorType(format);
    if (owner != mMemoryOwner) {
        if (owner) owner->AddRef();
        cIGZUnknown *old = mMemoryOwner;
        mMemoryOwner = owner;
        if (old) old->Release();
    }
    mExternalMemory = true;
    mReady = pixels != nullptr;
    mBytesPerPixel = static_cast<uint8_t>(cGZColorType(format).bits >> 3);
}

void cGZTransientBuffer::Commit()
{
    if (mExternalMemory && mReady && mTarget) mTarget->Commit(this);
}

bool cGZTransientBuffer::Realize()
{
    if (mReady) return true;
    if (mTarget) return mTarget->Realize(this);
    return false;
}

void cGZTransientBuffer::SetTarget(cIGZTransientBufferTarget *target)
{
    if (target == mTarget) return;
    if (target) target->AddRef();
    cIGZTransientBufferTarget *old = mTarget;
    mTarget = target;
    if (old) old->Detach(this);
    // Read again: the old target may have set another.
    if (mTarget) mTarget->Attach(this);
    if (old) old->Release();
}

cIGZTransientBufferTarget *cGZTransientBuffer::GetTarget()
{
    return mTarget;
}

uint32_t cGZTransientBuffer::GetLockTotal()
{
    return mLockTotal;
}
