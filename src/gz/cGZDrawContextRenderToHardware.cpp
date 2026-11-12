// cGZDrawContextRenderToHardware, the hardware draw context engine's base (src/gz/cGZDrawContextRenderToHardware.h;
// docs/subsystems/rendering.md, R4b).
#include "gz/cGZDrawContextRenderToHardware.h"

#include <bit>
#include <cmath>
#include <xmmintrin.h>

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"          // gz::AnyUint32
#include "gz/cGZRect.h"         // GZRectIntersect
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZPropertySetString.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef

namespace {

// The UI scale's globals, as cGZWinMgrW32::GetUIScale's: the minimum height scaled, the design height, the heights for
// the factors 2 and 3; the settings service; the clamp of its factor.
constexpr uint32_t kMinScaledHeight = 0x105f808, kDesignHeight = 0x10ecc88, kHeightFactor2 = 0x105f80c,
                   kHeightFactor3 = 0x105f810;
constexpr uint32_t kSettingsId = 0x0ac1887e, kSettingsIid = 0x4bb0d269;
constexpr uint32_t kMinFactor = 1, kMaxFactor = 3;

// A float rounded to the nearest integer, ties to even (the original's fistp, in the default rounding mode).
inline int32_t RoundToInt(float value) { return _mm_cvtss_si32(_mm_set_ss(value)); }

// A float times a double constant, rounded to float and then to the nearest integer (the original's fixed-point
// conversions: the product through double, stored as a float, fistp).
inline int32_t ScaledToInt(float value, double scale)
{
    return RoundToInt(static_cast<float>(static_cast<double>(value) * scale));
}

// SetTexture's scaled area edges: v (already less 0.5) rounded by the float bias trick (0.5 and 1.5 * 2^23 added, the
// integer read from the low mantissa bits), then one less where that equals v exactly.
int32_t AreaEdge(float unrounded)
{
    const float biased = (unrounded + 0.5f) + 12582912.0f;
    int32_t edge = std::bit_cast<int32_t>(biased) - 0x4b400000;
    if (static_cast<float>(edge - 1) == unrounded) edge -= 1;
    return edge;
}

// The C runtime's ceil / floor on a float through double, back to float, truncated (the original's pattern).
inline int32_t CeilToInt(float value) { return static_cast<int32_t>(static_cast<float>(std::ceil(static_cast<double>(value)))); }
inline int32_t FloorToInt(float value) { return static_cast<int32_t>(static_cast<float>(std::floor(static_cast<double>(value)))); }

// 0x373520 (ours, src/gz/cGZRect.h): `*out` = a & b when they overlap, else all zero. `out` may be `a`.
inline void Intersect(cGZRect *out, const cGZRect *a, const cGZRect *b) { GZRectIntersect(out, a, b); }

// ApplyClip's projection for an empty clip (the image's 0xe4ed20): everything at z 0.5, nothing on screen.
constexpr float kEmptyProjection[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5f, 1.0f};

// The UV inset of an unscaled texture (0xdb13d4): a 32nd of a texel.
constexpr float kTexelInset = 0.03125f;

} // namespace

float __stdcall LegacyCalculateUIScale(const cGZRect *bounds)
{
    ScopedRef settings{nullptr};
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kSettingsId, kSettingsIid, &settings.mObject);
    gz::AnyUint32 factor(1);
    const float height = static_cast<float>(bounds->bottom - bounds->top);
    float scale = 1.0f;
    if (!(*GameAddress<float>(kMinScaledHeight) > height)) {
        if (settings.mObject) {
            GZServiceString key("uiScaleFactor");
            As<ifaces::cIGZPropertySetString>(settings.mObject)->GetPropertyValue(&key, static_cast<cIGZAny *>(&factor));
        }
        if (factor.mValue < kMinFactor) factor.mValue = kMinFactor;
        else if (factor.mValue > kMaxFactor) factor.mValue = kMaxFactor;
        const float design = *GameAddress<float>(kDesignHeight);
        if (height > design) {
            if (factor.mValue == 2) scale = height / *GameAddress<float>(kHeightFactor2);
            else if (factor.mValue == 1) scale = height / design;
            else scale = height / *GameAddress<float>(kHeightFactor3);
        }
    }
    settings.ReleaseObject();
    return scale;
}

cGZDrawContextRenderToHardware::cGZDrawContextRenderToHardware()
    : mCount(0), mTarget(nullptr), mTexture(nullptr), mTextureSource(nullptr), mStateDirty(false), mClipDirty(false),
      mQuadBatch(false), mTextureClipped(false), mClipToArea(true), mGradientOn(false)
{
}

cGZDrawContextRenderToHardware::~cGZDrawContextRenderToHardware()
{
    if (mTexture) mTexture->Release();
    if (mTarget) mTarget->Release();
}

bool cGZDrawContextRenderToHardware::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kIid) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cGZDrawContextRenderToHardware::AddRef() { return ++mCount; }

uint32_t cGZDrawContextRenderToHardware::Release()
{
    const uint32_t count = --mCount;
    if (count == 0) delete this;
    return count;
}

bool cGZDrawContextRenderToHardware::SetTarget(cIGZBuffer *target)
{
    if (mTarget) {
        cIGZBuffer *const old = mTarget;
        mTarget = nullptr;
        old->Release();
    }
    if (target->QueryInterface(cIGZBuffer::kIid, reinterpret_cast<void **>(&mTarget))) {
        mTargetWidth = mTarget->GetWidth();
        mTargetHeight = mTarget->GetHeight();
        if (OnTargetSet()) {
            mStateDirty = true;
            mFlagB1 = 1;
            return true;
        }
    }
    if (mTarget) {
        cIGZBuffer *const old = mTarget;
        mTarget = nullptr;
        old->Release();
    }
    return false;
}

void cGZDrawContextRenderToHardware::Detach() { OnDetach(); }

cGZRect *cGZDrawContextRenderToHardware::GetBounds(cGZRect *bounds)
{
    *bounds = mTarget->GetBounds();
    return bounds;
}

void cGZDrawContextRenderToHardware::SetClip(const cGZRect *clip)
{
    mClip = *clip;
    cGZRect bounds;
    GetBounds(&bounds);
    const float scale = LegacyCalculateUIScale(&bounds);
    const cGZRect scaled{static_cast<int32_t>(static_cast<float>(bounds.left) / scale),
                         static_cast<int32_t>(static_cast<float>(bounds.top) / scale),
                         static_cast<int32_t>(static_cast<float>(bounds.right) / scale),
                         static_cast<int32_t>(static_cast<float>(bounds.bottom) / scale)};
    Intersect(&mClip, clip, &scaled);
    mClipDirty = true;
}

void cGZDrawContextRenderToHardware::SetColors(uint32_t first, uint32_t count, const uint32_t *argb)
{
    for (uint32_t i = 0; i < count; ++i) mColors[first + i] = argb[i];
}

void cGZDrawContextRenderToHardware::vf8(uint32_t value)
{
    const int32_t v = static_cast<int32_t>(value);
    mBlendIs2 = v == 2;
    mStateDirty = true;
    mBlend = v > 0 && v != 2;
    mBlendAbove2 = v >= 3;
}

void cGZDrawContextRenderToHardware::vf9(uint8_t value)
{
    mFlag49 = value;
    mStateDirty = true;
}

void cGZDrawContextRenderToHardware::vf10(uint8_t value)
{
    mModulate = value;
    mStateDirty = true;
}

void cGZDrawContextRenderToHardware::vf11(uint8_t value) { mFlag4a = value; }

void cGZDrawContextRenderToHardware::SetLineWidth(float) {}

void cGZDrawContextRenderToHardware::SetGradient(const float *coefficients, float x, float y)
{
    if (!coefficients) {
        mGradientOn = false;
        return;
    }
    constexpr double kFixed = 1044480.0; // 0xe4ed60: 255 * 2^12
    for (int i = 0; i < 4; ++i) {
        const int32_t dx = ScaledToInt(coefficients[4 + i], kFixed);
        const int32_t dy = ScaledToInt(coefficients[8 + i], kFixed);
        mGradientDx[i] = dx;
        mGradientDy[i] = dy;
        const float atOrigin = static_cast<float>(dx) * x + static_cast<float>(dy) * y;
        const int32_t base = RoundToInt(
            static_cast<float>(static_cast<double>(coefficients[i]) * kFixed - static_cast<double>(atOrigin)));
        mGradientBase[i] = static_cast<uint32_t>(base + 0x800) << 8;
    }
    mGradientOn = true;
}

bool cGZDrawContextRenderToHardware::SetTexture(cIGZUnknown *texture, float x, float y, float sx, float sy, cGZRect *area)
{
    bool reset = texture == nullptr;
    if (!reset && texture != mTextureSource) {
        if (mTexture) {
            cIGZBuffer *const old = mTexture;
            mTexture = nullptr;
            old->Release();
        }
        reset = !texture->QueryInterface(cIGZBuffer::kIid, reinterpret_cast<void **>(&mTexture));
        if (!reset) {
            FlushBatch();
            reset = !SetTextureSource(mTexture);
        }
        if (!reset) {
            mTiles.clear();
            mTileRects.clear();
            GetTextureTiles(&mTiles, &mTileRects);
            mTextureSource = texture;
            mStateDirty = true;
            mCurrentTile = -1;
        }
    }
    if (reset) {
        FlushBatch();
        mStateDirty = true;
        if (mClipToArea) {
            mClipToArea = false;
            mClipDirty = true;
        }
        SetTextureSource(nullptr);
        if (mTexture) {
            cIGZBuffer *const old = mTexture;
            mTexture = nullptr;
            old->Release();
        }
        mTextureSource = nullptr;
        return false;
    }

    mTextureX = x;
    mTextureY = y;
    mInverseScaleX = 1.0f / sx;
    mScaleX = sx;
    mScaleY = sy;
    mInverseScaleY = 1.0f / sy;
    mScaled = std::fabs(sx - 1.0f) + std::fabs(sy - 1.0f) > 1e-5f;
    const cGZRect texels = mTexture->GetBounds();
    cGZRect bounds;
    GetBounds(&bounds);
    if (LegacyCalculateUIScale(&bounds) > 1.0f) mScaled = true;
    mTextureWidth = static_cast<float>(texels.right - texels.left);
    mTextureHeight = static_cast<float>(texels.bottom - texels.top);
    if (mScaled) {
        area->left = AreaEdge(static_cast<float>(texels.left) * mScaleX - 0.5f);
        area->top = AreaEdge(static_cast<float>(texels.top) * mScaleY - 0.5f);
        area->right = AreaEdge(static_cast<float>(texels.right) * mScaleX - 0.5f);
        area->bottom = AreaEdge(static_cast<float>(texels.bottom) * mScaleY - 0.5f);
    } else {
        *area = texels;
    }
    const int32_t ox = static_cast<int32_t>(mTextureX), oy = static_cast<int32_t>(mTextureY);
    area->left += ox;
    area->right += ox;
    area->top += oy;
    area->bottom += oy;
    mTextureArea = *area;
    return false;
}

void cGZDrawContextRenderToHardware::ClearTextureClip() { mTextureClipped = false; }

void cGZDrawContextRenderToHardware::SetTextureClip(const cGZRect *clip)
{
    mTextureClipped = true;
    mTextureClip = *clip;
}

void cGZDrawContextRenderToHardware::vf17() { FlushBatch(); }

void cGZDrawContextRenderToHardware::vf18()
{
    FlushBatch();
    mStateDirty = true;
    mClipDirty = true;
    mCurrentTile = -1;
    mFlagB1 = 0;
}

void cGZDrawContextRenderToHardware::vf19() { mFlagB1 = 1; }

bool cGZDrawContextRenderToHardware::LockSurface(cGZDrawSurface *, uint32_t) { return false; }

void cGZDrawContextRenderToHardware::UnlockSurface() {}

uint32_t cGZDrawContextRenderToHardware::ChooseRoute(uint32_t kind, const float *points, uint32_t count)
{
    if (kind != 6 && kind != 3) return 1;
    if (mStateDirty) {
        FlushBatch();
        ApplyState();
        mStateDirty = false;
    }
    if (mClipDirty) {
        FlushBatch();
        ApplyClip();
    }
    if (kind == 6)
        DrawTriangleRoute(points, static_cast<int32_t>(count));
    else
        DrawRectRoute(points, static_cast<int32_t>(count));
    return 0;
}

void cGZDrawContextRenderToHardware::DrawSpans(const int32_t *, uint32_t) {}

void cGZDrawContextRenderToHardware::FlushBatch()
{
    if (mVertices.empty()) return;
    if (mQuadBatch)
        DrawQuads(mVertices.data(), static_cast<uint32_t>(mVertices.size()));
    else
        DrawTriangles(mVertices.data(), static_cast<uint32_t>(mVertices.size()));
    mVertices.clear();
}

void cGZDrawContextRenderToHardware::ApplyStateAndClip()
{
    ApplyState();
    ApplyClip();
    mStateDirty = false;
    mClipDirty = false;
    mCurrentTile = -1;
}

void cGZDrawContextRenderToHardware::ApplyClip()
{
    cGZRect r = mClip;
    mClipDirty = false;
    bool empty = false;
    if (mClipToArea) {
        const cGZRect &a = mTextureArea;
        if (r.right < a.left || r.left > a.right || r.bottom < a.top || r.top > a.bottom) {
            empty = true;
        } else {
            if (r.left <= a.left) r.left = a.left;
            if (r.top <= a.top) r.top = a.top;
            if (r.right >= a.right) r.right = a.right;
            if (r.bottom >= a.bottom) r.bottom = a.bottom;
        }
    }
    if (empty || r.right <= r.left || r.bottom <= r.top) {
        SetProjection(kEmptyProjection);
        return;
    }
    cGZRect viewport = r;
    cGZRect bounds;
    GetBounds(&bounds);
    const float scale = LegacyCalculateUIScale(&bounds);
    if (scale > 1.0f) {
        viewport.left = static_cast<int32_t>(static_cast<float>(viewport.left) * scale);
        viewport.top = static_cast<int32_t>(static_cast<float>(viewport.top) * scale);
        viewport.right = static_cast<int32_t>(static_cast<float>(viewport.right) * scale);
        viewport.bottom = static_cast<int32_t>(static_cast<float>(viewport.bottom) * scale);
    }
    SetViewport(&viewport);
    float m[16] = {};
    m[0] = 2.0f / static_cast<float>(r.right - r.left);
    m[5] = -2.0f / static_cast<float>(r.bottom - r.top);
    m[12] = -1.0f - static_cast<float>(r.left) * m[0];
    m[13] = 1.0f - static_cast<float>(r.top) * m[5];
    m[14] = 0.5f;
    m[15] = 1.0f;
    SetProjection(m);
}

uint32_t cGZDrawContextRenderToHardware::GradientColor(float x, float y)
{
    const uint32_t ix = static_cast<uint32_t>(ScaledToInt(x, 256.0));
    const uint32_t iy = static_cast<uint32_t>(ScaledToInt(y, 256.0));
    uint32_t channel[4];
    for (int i = 0; i < 4; ++i)
        channel[i] = mGradientBase[i] + static_cast<uint32_t>(mGradientDx[i]) * ix + static_cast<uint32_t>(mGradientDy[i]) * iy;
    return ((channel[3] & 0xfff00000u) << 4) + ((channel[1] >> 12) & 0xff000u) + ((channel[2] >> 4) & 0xff0000u) +
           (channel[0] >> 20);
}

void cGZDrawContextRenderToHardware::DrawTriangleRoute(const float *points, int32_t count)
{
    if (mQuadBatch) {
        if (!mVertices.empty()) DrawQuads(mVertices.data(), static_cast<uint32_t>(mVertices.size()));
        mVertices.clear();
        mQuadBatch = false;
    }
    const size_t first = mVertices.size();
    mVertices.resize(first + static_cast<size_t>(static_cast<uint32_t>(count)));
    const uint32_t colour = mTexture ? 0xffffffffu : mColors[0];
    for (int32_t i = 0; i < count; ++i) {
        Vertex &v = mVertices[first + i];
        v.x = points[2 * i];
        v.y = points[2 * i + 1];
        v.z = 0.5f;
        v.u = 0.0f;
        v.v = 0.0f;
        v.colour = mGradientOn ? GradientColor(points[2 * i], points[2 * i + 1]) : colour;
    }
    if (!mTexture) return;

    // ORIGINAL-BUG: the batch, these triangles included, is drawn and emptied before their texture coordinates are set,
    // so they are drawn once with coordinates 0 and the tile pass below draws empty batches. The polygon's bounds are
    // then read from the emptied batch's memory (still allocated: emptying keeps the capacity).
    const Vertex *const added = mVertices.data() + first;
    FlushBatch();
    mTexture->GetBounds(); // unused
    const cGZRect saved = mClip;
    float minX = added[0].x, maxX = minX, minY = added[0].y, maxY = minY;
    for (int32_t i = 1; i < count; ++i) {
        const float px = added[i].x, py = added[i].y;
        if (minX > px)
            minX = px;
        else
            maxX = px > maxX ? px : maxX;
        if (minY > py)
            minY = py;
        else
            maxY = py > maxY ? py : maxY;
    }
    cGZRect r{CeilToInt(minX - 0.5f), CeilToInt(minY - 0.5f), CeilToInt(maxX - 0.5f), CeilToInt(maxY - 0.5f)};
    Intersect(&r, &r, &mClip);
    const cGZRect texels{static_cast<int32_t>((static_cast<float>(r.left) - mTextureX) / mScaleX),
                         static_cast<int32_t>((static_cast<float>(r.top) - mTextureY) / mScaleY),
                         static_cast<int32_t>((static_cast<float>(r.right) - mTextureX) / mScaleX),
                         static_cast<int32_t>((static_cast<float>(r.bottom) - mTextureY) / mScaleY)};
    if (mTextureClipped) {
        const float cellWidth = static_cast<float>(mTextureClip.right - mTextureClip.left);
        const float cellHeight = static_cast<float>(mTextureClip.bottom - mTextureClip.top);
        const float stepX = cellWidth * mScaleX, stepY = cellHeight * mScaleY;
        const float perWidth = 1.0f / cellWidth, perHeight = 1.0f / cellHeight;
        const int32_t col0 = FloorToInt(static_cast<float>(texels.left) * perWidth);
        const int32_t col1 = CeilToInt(static_cast<float>(texels.right) * perWidth);
        const int32_t row1 = CeilToInt(static_cast<float>(texels.bottom) * perHeight);
        const int32_t row0 = FloorToInt(static_cast<float>(texels.top) * perHeight);
        for (int32_t row = row0; row < row1; ++row) {
            const float cellY = static_cast<float>(row) * stepY;
            for (int32_t col = col0; col < col1; ++col)
                DrawTiles(static_cast<float>(col) * stepX + mTextureX, cellY + mTextureY, &mTextureClip);
        }
    } else {
        if (!mClipToArea) {
            mClipToArea = true;
            ApplyClip();
        }
        DrawTiles(mTextureX, mTextureY, &texels);
    }
    mVertices.clear();
    SetClip(&saved);
}

void cGZDrawContextRenderToHardware::DrawTiles(float x, float y, const cGZRect *cell)
{
    Vertex *const vertices = mVertices.data();
    const int32_t count = static_cast<int32_t>(mVertices.size());
    const float inset = mScaled ? 0.0f : kTexelInset;
    const TileRect *rect = mTileRects.data();
    for (const Tile &tile : mTiles) {
        const float perWidth = 1.0f / (static_cast<float>(tile.width) * mScaleX);
        const float perHeight = 1.0f / (static_cast<float>(tile.height) * mScaleY);
        for (uint32_t j = 0; j < tile.count; ++j, ++rect) {
            const cGZRect &src = rect->source;
            if (!(cell->right > src.left && cell->left < src.right && cell->bottom > src.top && cell->top < src.bottom))
                continue;
            if (mCurrentTile != tile.index) {
                mCurrentTile = tile.index;
                SelectTile(tile.index, mScaled, 0);
            }
            const cGZRect &tex = rect->texels;
            for (int32_t i = 0; i < count; ++i) {
                Vertex &v = vertices[i];
                v.u = ((((v.x - x) - static_cast<float>(src.left)) + static_cast<float>(tex.left)) + inset) * perWidth;
                v.v = ((((v.y - y) - static_cast<float>(src.top)) + static_cast<float>(tex.top)) + inset) * perHeight;
            }
            const int32_t ox = static_cast<int32_t>(x), oy = static_cast<int32_t>(y);
            const cGZRect clip{static_cast<int32_t>(static_cast<float>(tex.left) * mScaleX) + ox,
                               static_cast<int32_t>(static_cast<float>(tex.top) * mScaleY) + oy,
                               static_cast<int32_t>(static_cast<float>(tex.right) * mScaleX) + ox,
                               static_cast<int32_t>(static_cast<float>(tex.bottom) * mScaleY) + oy};
            SetClip(&clip);
            mClipToArea = false;
            ApplyClip();
            DrawTriangles(mVertices.data(), static_cast<uint32_t>(mVertices.size()));
        }
    }
}

void cGZDrawContextRenderToHardware::DrawRectRoute(const float *points, int32_t count)
{
    if (!mQuadBatch) {
        if (!mVertices.empty()) DrawTriangles(mVertices.data(), static_cast<uint32_t>(mVertices.size()));
        mVertices.clear();
        mQuadBatch = true;
    }
    const int32_t rects = count >> 1;
    Vertex quad[4];
    for (Vertex &v : quad) {
        v.z = 0.5f;
        v.colour = mColors[0];
        v.u = 0.0f;
        v.v = 0.0f;
    }
    auto push = [&] {
        for (const Vertex &v : quad) mVertices.push_back(v);
    };

    if (!mTexture) {
        for (int32_t r = 0; r < rects; ++r) {
            const float *const p = points + 4 * r;
            const float x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3];
            quad[0].x = x0, quad[0].y = y0;
            quad[1].x = x0, quad[1].y = y1;
            quad[2].x = x1, quad[2].y = y1;
            quad[3].x = x1, quad[3].y = y0;
            if (mGradientOn) {
                quad[0].colour = GradientColor(x0, y0);
                quad[1].colour = GradientColor(x0, y1);
                quad[2].colour = GradientColor(x1, y1);
                quad[3].colour = GradientColor(x1, y0);
            }
            push();
        }
        return;
    }

    if (mClipToArea) {
        mClipToArea = false;
        FlushBatch();
        ApplyClip();
    }
    mTexelRects.resize(static_cast<uint32_t>(rects));
    for (int32_t r = 0; r < rects; ++r) {
        const float *const p = points + 4 * r;
        TexelRect &t = mTexelRects[r];
        t.left = (p[0] - mTextureX) * mInverseScaleX;
        t.top = (p[1] - mTextureY) * mInverseScaleY;
        t.right = (p[2] - mTextureX) * mInverseScaleX;
        t.bottom = (p[3] - mTextureY) * mInverseScaleY;
    }
    if (!mTextureClipped) {
        for (int32_t r = 0; r < rects; ++r) {
            TexelRect &t = mTexelRects[r];
            if (t.left < 0.0f) t.left = 0.0f;
            if (t.top < 0.0f) t.top = 0.0f;
            if (mTextureWidth < t.right) t.right = mTextureWidth;
            if (mTextureHeight < t.bottom) t.bottom = mTextureHeight;
        }
    }

    // The texture clip's cell (only read when clipped); a cell one texel across repeats that texel.
    bool oneColumn = false, oneRow = false;
    float cellWidth = 0, cellHeight = 0, stepX = 0, stepY = 0, perCellWidth = 0, perCellHeight = 0;
    if (mTextureClipped) {
        const int32_t w = mTextureClip.right - mTextureClip.left, h = mTextureClip.bottom - mTextureClip.top;
        oneColumn = w == 1;
        cellWidth = static_cast<float>(w);
        cellHeight = static_cast<float>(h);
        stepX = cellWidth * mScaleX;
        perCellWidth = 1.0f / cellWidth;
        stepY = cellHeight * mScaleY;
        perCellHeight = 1.0f / cellHeight;
        oneRow = h == 1;
    }
    const float inset = mScaled ? 0.0f : kTexelInset;

    auto selectTile = [&](int32_t index) {
        if (mCurrentTile == index) return;
        FlushBatch();
        mCurrentTile = index;
        SelectTile(index, mScaled, 0);
    };

    const TileRect *rect = mTileRects.data();
    for (const Tile &tile : mTiles) {
        const float perWidth = 1.0f / static_cast<float>(tile.width);
        const float perHeight = 1.0f / static_cast<float>(tile.height);
        for (uint32_t j = 0; j < tile.count; ++j, ++rect) {
            const cGZRect &src = rect->source, &tex = rect->texels;
            const float srcLeft = static_cast<float>(src.left), srcTop = static_cast<float>(src.top);
            const float srcRight = static_cast<float>(src.right), srcBottom = static_cast<float>(src.bottom);
            if (!mTextureClipped) {
                for (int32_t r = 0; r < rects; ++r) {
                    const TexelRect &t = mTexelRects[r];
                    const float left = t.left < srcLeft ? srcLeft : t.left;
                    const float top = t.top < srcTop ? srcTop : t.top;
                    const float right = srcRight < t.right ? srcRight : t.right;
                    const float bottom = srcBottom < t.bottom ? srcBottom : t.bottom;
                    if (!(left < right && top < bottom)) continue;
                    selectTile(tile.index);
                    const float x0 = mScaleX * left + mTextureX;
                    const float y0 = mScaleY * top + mTextureY;
                    const float y1 = mScaleY * bottom + mTextureY;
                    const float u0 = (((left - srcLeft) + static_cast<float>(tex.left)) + inset) * perWidth;
                    const float u1 = (((right - srcLeft) + static_cast<float>(tex.left)) + inset) * perWidth;
                    const float v0 = (((top - srcTop) + static_cast<float>(tex.top)) + inset) * perHeight;
                    const float v1 = (((bottom - srcTop) + static_cast<float>(tex.top)) + inset) * perHeight;
                    const float x1 = mScaleX * right + mTextureX;
                    quad[0].x = x0, quad[0].y = y0, quad[0].u = u0, quad[0].v = v0;
                    quad[1].x = x0, quad[1].y = y1, quad[1].u = u0, quad[1].v = v1;
                    quad[2].x = x1, quad[2].y = y1, quad[2].u = u1, quad[2].v = v1;
                    quad[3].x = x1, quad[3].y = y0, quad[3].u = u1, quad[3].v = v0;
                    push();
                }
                continue;
            }
            const cGZRect &clip = mTextureClip;
            if (!(src.left < clip.right && clip.left < src.right && src.top < clip.bottom && clip.top < src.bottom)) continue;
            selectTile(tile.index);
            for (int32_t r = 0; r < rects; ++r) {
                const TexelRect &t = mTexelRects[r];
                int32_t col0 = 0, col1 = 1, row0 = 0, row1 = 1;
                if (!oneColumn) {
                    col0 = FloorToInt(perCellWidth * t.left);
                    col1 = CeilToInt(perCellWidth * t.right);
                }
                if (!oneRow) {
                    row0 = FloorToInt(perCellHeight * t.top);
                    row1 = CeilToInt(perCellHeight * t.bottom);
                }
                for (int32_t row = row0; row < row1; ++row) {
                    const float rowOffset = static_cast<float>(row) * cellHeight;
                    for (int32_t col = col0; col < col1; ++col) {
                        const float clipTop = static_cast<float>(clip.top), clipLeft = static_cast<float>(clip.left);
                        const float colF = static_cast<float>(col);
                        const float shiftX = clipLeft - colF * cellWidth;
                        float bottom = (clipTop - rowOffset) + t.bottom;
                        float right = shiftX + t.right;
                        float left = shiftX + t.left;
                        if (oneColumn) {
                            right = clipLeft + 1.0f;
                            left = clipLeft;
                        }
                        float top = (clipTop - rowOffset) + t.top;
                        if (oneRow) {
                            bottom = clipTop + 1.0f;
                            top = clipTop;
                        }
                        if (left < clipLeft) left = clipLeft;
                        if (top < clipTop) top = clipTop;
                        if (static_cast<float>(clip.right) < right) right = static_cast<float>(clip.right);
                        if (static_cast<float>(clip.bottom) < bottom) bottom = static_cast<float>(clip.bottom);
                        if (left < srcLeft) left = srcLeft;
                        if (top < srcTop) top = srcTop;
                        if (srcRight < right) right = srcRight;
                        if (srcBottom < bottom) bottom = srcBottom;
                        if (!(left < right && top < bottom)) continue;
                        float x0, x1, u0, u1, y0, y1, v0, v1;
                        if (!oneColumn) {
                            x0 = (left - clipLeft) * mScaleX + mTextureX + colF * stepX;
                            x1 = (right - clipLeft) * mScaleX + mTextureX + colF * stepX;
                            u0 = (left - srcLeft) + static_cast<float>(tex.left) + inset;
                            u1 = (right - srcLeft) + static_cast<float>(tex.left) + inset;
                        } else {
                            x0 = mScaleX * t.left + mTextureX;
                            x1 = mScaleX * t.right + mTextureX;
                            u0 = u1 = static_cast<float>(tex.left) + 0.5f;
                        }
                        if (!oneRow) {
                            const float rowY = static_cast<float>(row) * stepY;
                            y0 = (top - clipTop) * mScaleY + mTextureY + rowY;
                            y1 = (bottom - clipTop) * mScaleY + mTextureY + rowY;
                            v0 = (top - srcTop) + static_cast<float>(tex.top) + inset;
                            v1 = (bottom - srcTop) + static_cast<float>(tex.top) + inset;
                        } else {
                            v0 = v1 = static_cast<float>(tex.top) + 0.5f;
                            y1 = mScaleY * t.bottom + mTextureY;
                            y0 = mScaleY * t.top + mTextureY;
                        }
                        u0 *= perWidth;
                        u1 *= perWidth;
                        v0 *= perHeight;
                        v1 *= perHeight;
                        quad[0].x = x0, quad[0].y = y0, quad[0].u = u0, quad[0].v = v0;
                        quad[1].x = x0, quad[1].y = y1, quad[1].u = u0, quad[1].v = v1;
                        quad[2].x = x1, quad[2].y = y1, quad[2].u = u1, quad[2].v = v1;
                        quad[3].x = x1, quad[3].y = y0, quad[3].u = u1, quad[3].v = v0;
                        push();
                    }
                }
            }
        }
    }
}
