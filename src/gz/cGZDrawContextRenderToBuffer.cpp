// cGZDrawContextRenderToBuffer, the software draw context engine (src/gz/cGZDrawContextRenderToBuffer.h;
// docs/subsystems/rendering.md, R5b).
#include "gz/cGZDrawContextRenderToBuffer.h"

#include <bit>
#include <cmath>
#include <new>
#include <vector>
#include <xmmintrin.h>

#include "game/original.h"
#include "gz/GZScanConvert.h"

using namespace nGZDrawSoftRender;

namespace {

// A float rounded to the nearest integer, ties to even (the original's fistp, in the default rounding mode).
inline int32_t RoundToInt(float value) { return _mm_cvtss_si32(_mm_set_ss(value)); }

// A float times a double constant, rounded to float and then to the nearest integer, as the original computes its
// 16.16 values (the product through double, stored as a float, fistp).
inline int32_t ScaledToInt(float value, double scale)
{
    return RoundToInt(static_cast<float>(static_cast<double>(value) * scale));
}

// SetTexture's area edges: v - 0.5 (through double) rounded by the float bias trick (1.5 * 2^23 added, the integer
// read from the low mantissa bits), then one less where that equals the unrounded value exactly.
int32_t AreaEdge(float value)
{
    const float unrounded = static_cast<float>(static_cast<double>(value) - 0.5);
    const float biased = (unrounded + 0.5f) + 12582912.0f;
    int32_t edge = std::bit_cast<int32_t>(biased) - 0x4b400000;
    if (static_cast<float>(edge - 1) == unrounded) edge -= 1;
    return edge;
}

} // namespace

cGZDrawContextRenderToBuffer::cGZDrawContextRenderToBuffer() = default;

cGZDrawContextRenderToBuffer::~cGZDrawContextRenderToBuffer()
{
    if (mRenderer) delete mRenderer;
    if (mTexture) mTexture->Release();
    if (mTarget) mTarget->Release();
}

bool cGZDrawContextRenderToBuffer::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kIid) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cGZDrawContextRenderToBuffer::AddRef() { return ++mCount; }

uint32_t cGZDrawContextRenderToBuffer::Release()
{
    const uint32_t count = --mCount;
    if (count == 0) {
        this->~cGZDrawContextRenderToBuffer();
        ::operator delete(this, kSize);
    }
    return count;
}

bool cGZDrawContextRenderToBuffer::SetTarget(cIGZBuffer *target)
{
    Detach();
    if (mTarget) {
        cIGZBuffer *const old = mTarget;
        mTarget = nullptr;
        old->Release();
    }
    if (!target->QueryInterface(cIGZBuffer::kIid, reinterpret_cast<void **>(&mTarget))) return false;
    // A format without a renderer lets the target go still locked (as the original does).
    bool supported = false;
    if (mTarget->Lock(kLockWrite)) {
        supported = true;
        switch (mTarget->GetNativeColorType().format) {
        case 2: mRenderer = new cSpanRender8; break;
        case 4:
        case 6: mRenderer = new cSpanRender1555; break;
        case 5: mRenderer = new cSpanRender565; break;
        case 7: mRenderer = new cSpanRender4444; break;
        case 8:
        case 9: mRenderer = new cSpanRender8888; break;
        default: supported = false; break;
        }
    }
    if (supported) {
        mSurface.width = mTarget->GetWidth();
        mSurface.height = mTarget->GetHeight();
        mSurface.format = mTarget->GetNativeColorType().format;
        mSurface.bits = static_cast<uint8_t *>(mTarget->GetColorSurfaceBits());
        mSurface.stride = static_cast<int32_t>(mTarget->GetColorSurfaceStride());
        mSurface.bytesPerPixel = mTarget->GetBytesPerPixel();
        mTargetWidth = mSurface.width;
        mTargetHeight = mSurface.height;
        mBytesPerPixel = mTarget->GetBytesPerPixel();
        mTextureClipped = false;
        mLockLevel = 2;
        mBatchLevel = 2;
        return true;
    }
    if (mTarget) {
        cIGZBuffer *const old = mTarget;
        mTarget = nullptr;
        old->Release();
    }
    return false;
}

void cGZDrawContextRenderToBuffer::Detach()
{
    if (mRenderer) {
        delete mRenderer;
        mRenderer = nullptr;
    }
    if (mTarget) {
        mSurfaceLevel = 0;
        mBatchLevel = 0;
        UpdateLock();
        if (mTarget) {
            cIGZBuffer *const old = mTarget;
            mTarget = nullptr;
            old->Release();
        }
    }
}

cGZRect *cGZDrawContextRenderToBuffer::GetBounds(cGZRect *bounds)
{
    *bounds = mTarget->GetBounds();
    return bounds;
}

void cGZDrawContextRenderToBuffer::SetClip(const cGZRect *clip) { mClip = *clip; }

void cGZDrawContextRenderToBuffer::SetColors(uint32_t first, uint32_t count, const uint32_t *argb)
{
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t color = argb[i];
        mColors[first + i] = color;
        mNativeColors[first + i] = mTarget->ConvertRGBAValueToNative(
            static_cast<uint8_t>(color >> 16), static_cast<uint8_t>(color >> 8), static_cast<uint8_t>(color),
            static_cast<uint8_t>(color >> 24));
    }
}

void cGZDrawContextRenderToBuffer::vf8(uint32_t value) { mBlend = static_cast<int32_t>(value) > 0; }
void cGZDrawContextRenderToBuffer::vf9(uint8_t value) { mFlag42 = value; }
void cGZDrawContextRenderToBuffer::vf10(uint8_t value) { mModulate = value; }
void cGZDrawContextRenderToBuffer::vf11(uint8_t value) { mAntialias = value; }
void cGZDrawContextRenderToBuffer::SetLineWidth(float) {}

void cGZDrawContextRenderToBuffer::SetGradient(const float *coefficients, float x, float y)
{
    if (!coefficients) {
        mGradientOn = false;
        return;
    }
    // The coefficients are colour channels (0..1) at the origin and per pixel in x and y (B, G, R, A each); in 16.16
    // with 255 as 1.0, and the origin moved to (x, y).
    constexpr double kScale = 16711680.0; // 255 * 65536
    for (int i = 0; i < 4; ++i) {
        const int32_t dx = ScaledToInt(coefficients[4 + i], kScale);
        mGradient.dx[i] = dx;
        const int32_t dy = ScaledToInt(coefficients[8 + i], kScale);
        mGradient.dy[i] = dy;
        const int32_t base = ScaledToInt(coefficients[i], kScale) + 0x8000;
        mGradient.base[i] = static_cast<int32_t>(static_cast<float>(base) -
                                                 (static_cast<float>(dx) * x + static_cast<float>(dy) * y));
    }
    mGradientOn = true;
}

bool cGZDrawContextRenderToBuffer::SetTexture(cIGZUnknown *texture, float x, float y, float sx, float sy,
                                              cGZRect *area)
{
    if (mTexture) {
        cIGZBuffer *const old = mTexture;
        mTexture = nullptr;
        old->Release();
    }
    if (!texture) return false;
    if (!texture->QueryInterface(cIGZBuffer::kIid, reinterpret_cast<void **>(&mTexture))) return true;

    const cGZRect bounds = mTexture->GetBounds();
    constexpr double kOne = 65536.0;
    mScaleX = ScaledToInt(sx, kOne);
    mScaleY = ScaledToInt(sy, kOne);
    mStepX = RoundToInt(static_cast<float>(kOne / static_cast<double>(sx)));
    mStepY = RoundToInt(static_cast<float>(kOne / static_cast<double>(sy)));
    if (mScaleX == 0x10000 && mScaleY == 0x10000) {
        mTextureX = RoundToInt(x);
        mTextureY = RoundToInt(y);
        mScaled = false;
        *area = bounds;
        area->left += mTextureX;
        area->right += mTextureX;
        area->top += mTextureY;
        area->bottom += mTextureY;
    } else {
        mTextureX = RoundToInt(static_cast<float>(mStepX) * x);
        mTextureY = RoundToInt(static_cast<float>(mStepY) * y);
        mScaled = true;
        area->left = AreaEdge(x);
        area->top = AreaEdge(y);
        area->right = AreaEdge(static_cast<float>(bounds.right - bounds.left) * sx + x);
        area->bottom = AreaEdge(static_cast<float>(bounds.bottom - bounds.top) * sy + y);
    }
    mTextureAreaWidth = area->right - area->left;
    mTextureAreaHeight = area->bottom - area->top;
    return true;
}

void cGZDrawContextRenderToBuffer::ClearTextureClip() { mTextureClipped = false; }

void cGZDrawContextRenderToBuffer::SetTextureClip(const cGZRect *clip)
{
    mTextureClipped = true;
    mTextureClip = *clip;
}

void cGZDrawContextRenderToBuffer::vf17() {}

void cGZDrawContextRenderToBuffer::vf18()
{
    mBatchLevel = 0;
    UpdateLock();
}

void cGZDrawContextRenderToBuffer::vf19()
{
    mBatchLevel = 2;
    UpdateLock();
}

bool cGZDrawContextRenderToBuffer::LockSurface(cGZDrawSurface *surface, uint32_t readOnly)
{
    mSurfaceLevel = (static_cast<uint8_t>(readOnly) ^ 1) + 1;
    UpdateLock();
    *surface = mSurface;
    return true;
}

void cGZDrawContextRenderToBuffer::UnlockSurface()
{
    mSurfaceLevel = 0;
    UpdateLock();
}

uint32_t cGZDrawContextRenderToBuffer::ChooseRoute(uint32_t kind, const float *points, uint32_t count)
{
    if (!mAntialias) return 2;
    switch (kind) {
    case 6: // triangles
        if (static_cast<int32_t>(count) >= 3) {
            for (uint32_t n = count / 3; n != 0; --n, points += 6) FillPolygon(points, 3);
        }
        return 0;
    case 7: // quads
        if (static_cast<int32_t>(count) >= 4) {
            for (uint32_t n = count >> 2; n != 0; --n, points += 8) FillPolygon(points, 4);
        }
        return 0;
    case 8: // one polygon
        FillPolygon(points, static_cast<int32_t>(count));
        return 0;
    default:
        return 1;
    }
}

void cGZDrawContextRenderToBuffer::DrawSpans(const int32_t *spanWords, uint32_t count)
{
    const uint32_t *const spans = reinterpret_cast<const uint32_t *>(spanWords);
    if (!mTexture) {
        if (mGradientOn) {
            if (mBlend) mRenderer->BlendGradient(spans, count, &mSurface, &mGradient);
            else mRenderer->FillGradient(spans, count, &mSurface, &mGradient);
        } else if (mBlend) {
            mRenderer->Blend(spans, count, &mSurface, mNativeColors[0], mColors[0]);
        } else {
            mRenderer->Fill(spans, count, &mSurface, mNativeColors[0]);
        }
        return;
    }

    if (!mTexture->Lock(kLockTexture)) return;
    cGZDrawSurface texture;
    texture.width = mTexture->GetWidth();
    texture.height = mTexture->GetHeight();
    texture.format = mTexture->GetNativeColorType().format;
    texture.bits = static_cast<uint8_t *>(mTexture->GetColorSurfaceBits());
    texture.stride = static_cast<int32_t>(mTexture->GetColorSurfaceStride());
    texture.bytesPerPixel = mTexture->GetBytesPerPixel();

    TextureParams params = {};
    switch (texture.format) {
    case 7: params.formatIndex = 0; break; // 4444
    case 6: params.formatIndex = 1; break; // 1555
    case 4: params.formatIndex = 2; break; // 1555
    case 5: params.formatIndex = 3; break; // 565
    case 9: params.formatIndex = 4; break; // 888
    case 8: params.formatIndex = 5; break; // 8888
    case 2: params.formatIndex = 6; break; // 8-bit
    default: mTexture->Unlock(kLockTexture); return;
    }
    params.hasKey = mTexture->GetTransparentColor(&params.key) ? 1 : 0;
    if (params.hasKey) {
        switch (params.formatIndex) {
        case 0: params.key &= 0xfff; break;
        case 1:
        case 2: params.key &= 0x7fff; break;
        case 3: params.key &= 0xffff; break;
        case 4:
        case 5: params.key &= 0xffffff; break;
        case 6: params.hasKey = 0; break; // no transparent colour for palette textures
        }
    }
    params.color = mColors[0];
    uint32_t mode;
    if (mBlend) mode = mModulate ? 3 + (mFlag42 != 0 ? 1 : 0) : 1;
    else mode = mModulate ? 2 : 0;

    if (mTextureClipped) {
        // The clip rectangle of the texture tiled over the spans.
        const uint32_t clipWidth = static_cast<uint32_t>(mTextureClip.right - mTextureClip.left);
        const uint32_t clipHeight = static_cast<uint32_t>(mTextureClip.bottom - mTextureClip.top);
        texture.bits += static_cast<uint32_t>(mTextureClip.left) * texture.bytesPerPixel +
                        static_cast<uint32_t>(mTextureClip.top) * static_cast<uint32_t>(texture.stride);
        texture.width = clipWidth;
        texture.height = clipHeight;
        // The origin as a non-negative offset into the tile (unsigned, from the largest multiple below 2^31).
        params.offsetX = static_cast<int32_t>((0x7fffffffu - 0x7fffffffu % clipWidth - static_cast<uint32_t>(mTextureX)) %
                                              clipWidth);
        params.offsetY = static_cast<int32_t>(
            (0x7fffffffu - 0x7fffffffu % clipHeight - static_cast<uint32_t>(mTextureY)) % clipHeight);
        const TexelSpan *tiled = mRenderer->TiledSpans(spans, count, &mSurface, &texture, &params, mSurface.bytesPerPixel);
        mRenderer->DrawTexture(tiled, count, &mSurface, &texture, &params, mode + 10);
    } else if (mScaled) {
        params.offsetX = (mStepX >> 1) - mTextureX;
        params.offsetY = (mStepY >> 1) - mTextureY;
        params.stepX = mStepX;
        params.stepY = mStepY;
        const TexelSpan *scaled = mRenderer->ScaledSpans(spans, count, &mSurface, &texture, &params, mSurface.bytesPerPixel);
        mRenderer->DrawTexture(scaled, count, &mSurface, &texture, &params, mode + 5);
    } else {
        texture.bits -= static_cast<uint32_t>(mTextureX) * texture.bytesPerPixel +
                        static_cast<uint32_t>(mTextureY) * static_cast<uint32_t>(texture.stride);
        mRenderer->DrawTexture(spans, count, &mSurface, &texture, &params, mode);
    }
    mTexture->Unlock(kLockTexture);
}

void cGZDrawContextRenderToBuffer::UpdateLock()
{
    const int32_t level = mSurfaceLevel > mBatchLevel ? mSurfaceLevel : mBatchLevel;
    if (level == mLockLevel) return;
    if (mLockLevel == 1) mTarget->Unlock(kLockRead);
    else if (mLockLevel == 2) mTarget->Unlock(kLockWrite);
    mLockLevel = level;
    if (level == 0) return;
    if (level == 1) mTarget->Lock(kLockRead);
    else if (level == 2) mTarget->Lock(kLockWrite);
    mSurface.width = mTarget->GetWidth();
    mSurface.height = mTarget->GetHeight();
    mSurface.format = mTarget->GetNativeColorType().format;
    mSurface.bits = static_cast<uint8_t *>(mTarget->GetColorSurfaceBits());
    mSurface.stride = static_cast<int32_t>(mTarget->GetColorSurfaceStride());
    mSurface.bytesPerPixel = mTarget->GetBytesPerPixel();
}

void cGZDrawContextRenderToBuffer::FillPolygon(const float *points, int32_t count)
{
    if (count < 3) return;
    std::vector<PolygonSpan> spans;
    // The points in subsamples (8 per pixel), and their bounds.
    std::vector<float> scaled(static_cast<size_t>(count) * 2);
    scaled[0] = points[0] * 8.0f;
    scaled[1] = points[1] * 8.0f;
    float minX = scaled[0], maxX = scaled[0], minY = scaled[1], maxY = scaled[1];
    for (int32_t i = 1; i < count; ++i) {
        const float px = points[2 * i] * 8.0f;
        const float py = points[2 * i + 1] * 8.0f;
        scaled[2 * i] = px;
        scaled[2 * i + 1] = py;
        if (minX > px) minX = px;
        else maxX = px > maxX ? px : maxX;
        if (minY > py) minY = py;
        else maxY = py > maxY ? py : maxY;
    }
    const auto ceilToInt = [](float v) {
        return static_cast<int32_t>(static_cast<float>(std::ceil(static_cast<double>(v - 0.5f))));
    };
    const int32_t minPixelX = ceilToInt(minX) >> 3;
    const int32_t minPixelY = ceilToInt(minY) >> 3;
    const int32_t endPixelX = (ceilToInt(maxX) + 7) >> 3;
    const int32_t endPixelY = (ceilToInt(maxY) + 7) >> 3;

    GZScanConvertPolygon(&spans, scaled.data(), count);

    mTarget->GetWidth(); // asked, not used
    const int32_t height = static_cast<int32_t>(mTarget->GetHeight());
    const int32_t left = minPixelX >= mClip.left ? minPixelX : mClip.left;
    const int32_t right = endPixelX <= mClip.right ? endPixelX : mClip.right;
    const int32_t top = minPixelY >= mClip.top ? minPixelY : mClip.top;
    const int32_t bottom = endPixelY <= mClip.bottom ? endPixelY : mClip.bottom;
    if (!(left < right) || !(top < bottom)) return;

    // Coverage of the pixel row being gathered, 0..64 subsamples per pixel.
    std::vector<uint8_t> coverage(static_cast<uint32_t>(right - left));
    const PolygonSpan *span = spans.data();
    const PolygonSpan *const end = span + spans.size();
    int32_t row = -1;
    for (;;) {
        const int32_t y = span == end ? -1 : span->y;
        const int32_t spanRow = y >> 3;
        if (spanRow != row) {
            if (row >= 0 && row < height) {
                // The finished row: colour 0 over each pixel by its coverage.
                const int32_t red = (mColors[0] >> 16) & 0xff;
                const int32_t green = (mColors[0] >> 8) & 0xff;
                const int32_t blue = mColors[0] & 0xff;
                int32_t x = left;
                do {
                    const int32_t weight = (coverage[x - left] * 0xff) >> 6;
                    if (weight != 0) {
                        if (weight == 0xff) {
                            mTarget->SetPixel(x, row, mNativeColors[0]);
                        } else {
                            const uint32_t pixel = mTarget->GetPixel(x, row);
                            uint8_t r, g, b;
                            mTarget->ConvertNativeValueToRGB(pixel, &r, &g, &b);
                            r = static_cast<uint8_t>(r + (((red - r) * weight + 0x80) >> 8));
                            g = static_cast<uint8_t>(g + (((green - g) * weight + 0x80) >> 8));
                            b = static_cast<uint8_t>(b + (((blue - b) * weight + 0x80) >> 8));
                            mTarget->SetPixel(x, row, mTarget->ConvertRGBValueToNative(r, g, b));
                        }
                    }
                    coverage[x - left] = 0;
                } while (++x < right);
            }
            row = spanRow;
        }
        // The original also reads the end of the list here when the clip's top is negative; what it would add is
        // dropped (the loop ends right after), so ours skips it.
        if (span != end && y >= top * 8 && y < bottom * 8) {
            const int32_t x0 = span->x0 >= left * 8 ? span->x0 : left * 8;
            const int32_t x1 = span->x1 <= right * 8 ? span->x1 : right * 8;
            if (x0 < x1) {
                const int32_t first = x0 >> 3, last = x1 >> 3;
                if (first == last) {
                    coverage[first - left] = static_cast<uint8_t>(coverage[first - left] + (x1 - x0));
                } else {
                    coverage[first - left] = static_cast<uint8_t>(coverage[first - left] + 8 - (x0 & 7));
                    for (int32_t p = first + 1; p < last; ++p) coverage[p - left] = static_cast<uint8_t>(coverage[p - left] + 8);
                    if (x1 & 7) coverage[last - left] = static_cast<uint8_t>(coverage[last - left] + (x1 & 7));
                }
            }
        }
        if (span == end) break;
        ++span;
    }
}
