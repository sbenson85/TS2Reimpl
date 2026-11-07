#include "gz/cBrushes.h"

#include "gz/GraphicModeSearch.h"
#include "gz/GraphicServices.h"
#include "gz/RenderStates.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnum.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cGZEventReflector.h"
#include "ifaces/cIGZCOM.h"
#include "gz/cImageManipDX.h"
#include "game/vtable.h"
#include "hook/hooks.h"
#include "rz/StaticGetters.h"
#include "sg/TextureDescription.h"

#include <cmath>
#include <cstring>

namespace nGZGraphic4 {

namespace {

constexpr uint32_t kStride = 5; // floats (words) per vertex: the cursors step 0x14 bytes

uint32_t DefaultColour() { return *GameAddress<uint32_t>(kDefaultColourCell); }

// The triangle fan {0, i + 1, i + 2}, `triangles` of them (the index words are 16 bits wide).
void WriteFan(uint16_t *out, int32_t triangles)
{
    for (int32_t i = 0; i < triangles; ++i) {
        const uint16_t base = uint16_t(i);
        *out++ = 0;
        *out++ = uint16_t(base + 1);
        *out++ = uint16_t(base + 2);
    }
}

// The table interpolated at `t`: the entry at the wrapped integer part and the next one, each byte as a fraction of 255,
// blended by the fractional part, rounded by the 49152.0 trick (a float add leaving the value times 256 in the low bits;
// the byte is that clamped to 0..255).
uint32_t Interpolate(const uint32_t *table, uint32_t size, float t)
{
    const uint32_t whole = uint32_t(int32_t(t));
    const uint8_t *const from = reinterpret_cast<const uint8_t *>(table + whole % size);
    const uint8_t *const to = reinterpret_cast<const uint8_t *>(table + (whole + 1) % size);
    const float fraction = t - float(int32_t(whole));
    uint32_t colour = 0;
    for (int k = 3; k >= 0; --k) {
        const float a = float(from[k]) / 255.0f;
        const float b = float(to[k]) / 255.0f;
        const float blended = fraction * (b - a) + a;
        const float biased = blended + 49152.0f;
        uint32_t bits;
        std::memcpy(&bits, &biased, 4);
        const int32_t value = int32_t(bits + 0xb8c00000u);
        const uint32_t low = uint32_t(~(value >> 31)) & uint32_t(value);
        const uint32_t high = uint32_t((0xff - value) >> 31);
        colour = (colour << 8) | ((low | high) & 0xff);
    }
    return colour;
}

} // namespace

// ---- cColorBrush ----

cColorBrush *__cdecl cColorBrush::Create() { return new cColorBrush; }

bool cColorBrush::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != 1 && riid != kIidColorBrush && riid != kIidBrush) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cColorBrush::AddRef() { return ++mRefCount; }

uint32_t cColorBrush::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

uint32_t cColorBrush::GetStateBlock(cIDraw2D *, cIDeviceDX *) { return 0; }

void cColorBrush::Draw(cIDraw2D *draw2d, uint32_t, uint32_t count, const float *points, const float *values)
{
    const int32_t n = int32_t(count);
    float *positions;
    uint32_t *colours;
    float *uvs;
    uint16_t *indices;
    if (!draw2d->LockBatch(count, uint32_t((n - 2) * 3), &positions, &colours, &uvs, &indices)) return;
    for (int32_t i = 0; i < n; ++i) {
        uint32_t colour = DefaultColour();
        if (!mColours.empty()) colour = Interpolate(mColours.data(), uint32_t(mColours.size()), values[i]);
        *colours = colour;
        colours += kStride;
        uvs[0] = 0.0f;
        uvs[1] = 0.0f;
        uvs += kStride;
        positions[0] = points[i * 2];
        positions[1] = points[i * 2 + 1];
        positions += kStride;
    }
    WriteFan(indices, n - 2);
    draw2d->DrawBatch();
}

void cColorBrush::Resize(uint32_t count) { mColours.resize(count, DefaultColour()); }

void cColorBrush::SetColor(uint32_t index, const uint32_t *colour) { mColours[index] = *colour; }

uint32_t cColorBrush::Size() { return uint32_t(mColours.size()); }

void cColorBrush::GetColor(uint32_t *out, uint32_t index) { *out = mColours[index]; }

// ---- cOutlineColorBrush ----

cOutlineColorBrush *__cdecl cOutlineColorBrush::Create()
{
    cOutlineColorBrush *const brush = new cOutlineColorBrush;
    brush->mOutline = DefaultColour();
    brush->mFill = DefaultColour();
    return brush;
}

bool cOutlineColorBrush::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != 1 && riid != kIidBrush && riid != kIidOutlineColorBrush) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cOutlineColorBrush::AddRef() { return ++mRefCount; }

uint32_t cOutlineColorBrush::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

uint32_t cOutlineColorBrush::GetStateBlock(cIDraw2D *, cIDeviceDX *) { return 0; }

void cOutlineColorBrush::Draw(cIDraw2D *draw2d, uint32_t kind, uint32_t count, const float *points, const float *)
{
    const int32_t n = int32_t(count);
    float *positions;
    uint32_t *colours;
    float *uvs;
    uint16_t *indices;
    if (draw2d->LockBatch(count, uint32_t((n - 2) * 3), &positions, &colours, &uvs, &indices)) {
        const uint32_t colour = kind == 0 ? mFill : mOutline;
        for (int32_t i = 0; i < n; ++i) {
            *colours = colour;
            colours += kStride;
            uvs[0] = 0.0f;
            uvs[1] = 0.0f;
            uvs += kStride;
            positions[0] = points[i * 2];
            positions[1] = points[i * 2 + 1];
            positions += kStride;
        }
        WriteFan(indices, n - 2);
        draw2d->DrawBatch();
    }
    if (kind != 0) return;
    // The outline: for each edge a quad, the edge and the edge moved by its normal (the edge's length is 1 / the
    // reciprocal of the length of the vector; the double square root is rounded to a float).
    if (!draw2d->LockBatch(uint32_t(n * 4), uint32_t(n * 6), &positions, &colours, &uvs, &indices)) return;
    for (int32_t i = 0; i < n; ++i) {
        const float px = points[i * 2];
        const float py = points[i * 2 + 1];
        const int32_t next = (i + 1) % n;
        const float qx = points[next * 2];
        const float qy = points[next * 2 + 1];
        const float dx = qx - px;
        const float dy = qy - py;
        const float squared = dy * dy + dx * dx;
        const float length = float(std::sqrt(double(squared)));
        const float inverse = 1.0f / length;
        const float nx = dy * inverse;
        const float ny = -(dx * inverse);
        const float xs[4] = {px, qx, qx - nx, px - nx};
        const float ys[4] = {py, qy, qy - ny, py - ny};
        for (int v = 0; v < 4; ++v) {
            *colours = mOutline;
            colours += kStride;
            uvs[0] = 0.0f;
            uvs[1] = 0.0f;
            uvs += kStride;
            positions[0] = xs[v];
            positions[1] = ys[v];
            positions += kStride;
        }
        const uint16_t base = uint16_t(i << 2);
        indices[0] = base;
        indices[1] = uint16_t(base + 1);
        indices[2] = uint16_t(base + 2);
        indices[3] = base;
        indices[4] = uint16_t(base + 2);
        indices[5] = uint16_t(base + 3);
        indices += 6;
    }
    draw2d->DrawBatch();
}

void cOutlineColorBrush::SetOutlineColor(const uint32_t *colour) { mOutline = *colour; }

void cOutlineColorBrush::SetFillColor(const uint32_t *colour) { mFill = *colour; }

void cOutlineColorBrush::GetOutlineColor(uint32_t *out) { *out = mOutline; }

void cOutlineColorBrush::GetFillColor(uint32_t *out) { *out = mFill; }

// ---- cPatternBrush ----

namespace {

constexpr uint32_t kTextureClass = 0xbb892198; // the device texture the brush makes (asked for cITexture)
constexpr uint32_t kFormatUsage = 4;           // the usage word GetStateBlock asks the enumeration's formats for
constexpr uint32_t kUploadPool = 1, kDrawPool = 2; // cITexture::Create's last word: the texture filled from memory, then the drawn one
constexpr uint32_t kStateBlockKind = 3;        // CreateStateBlock's first argument
constexpr uint8_t kTextureBlendId = 0x19, kTextureFilterId = 0x1e, kTextureMipFilterId = 0x1f;

// A render state command for the state cache (zeroed, the state's game vtable, id and size; cDraw2D's commands).
template <class State> State Command(uint8_t id, const void *vtable)
{
    State state{};
    state.mVTable = vtable;
    state.mId = id;
    state.mSize = static_cast<uint16_t>(sizeof(State));
    return state;
}

// The range's copy constructor (0x167180): the cursor cloned and counted, then the ends counted.
cPixelFormatRange CopyFormats(const cPixelFormatRange &range)
{
    cPixelFormatRange copy;
    copy.mCurrent = range.mCurrent->Clone();
    if (copy.mCurrent) copy.mCurrent->AddRef();
    copy.mBegin = range.mBegin;
    if (copy.mBegin) copy.mBegin->AddRef();
    copy.mEnd = range.mEnd;
    if (copy.mEnd) copy.mEnd->AddRef();
    return copy;
}

// The 2 x 2 matrices of SetAngle (a, b, c, d in memory order).
void Rotation(float *m, float angle)
{
    const float c = float(std::cos(double(angle)));
    const float s = float(std::sin(double(angle)));
    m[0] = c;
    m[1] = -s;
    m[2] = s;
    m[3] = c;
}

void Scaling(float *m, float x, float y)
{
    m[0] = x;
    m[1] = 0.0f;
    m[2] = 0.0f;
    m[3] = y;
}

void Multiply(float *out, const float *a, const float *b)
{
    float r[4];
    r[0] = a[0] * b[0] + b[1] * a[2];
    r[2] = b[3] * a[2] + b[2] * a[0];
    r[1] = a[1] * b[0] + b[1] * a[3];
    r[3] = a[1] * b[2] + b[3] * a[3];
    std::memcpy(out, r, sizeof(r));
}

} // namespace

cPatternBrush *__cdecl cPatternBrush::Create() { return new cPatternBrush; }

bool cPatternBrush::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != 1 && riid != kIidPatternBrush && riid != kIidBrush) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cPatternBrush::AddRef() { return ++mRefCount; }

uint32_t cPatternBrush::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

// The texture for the bitmap: the closest format the display has, a texture of the fitted size in the upload pool
// filled from the bitmap, then the brush's own texture in the draw pool copied from it. A false return leaves whatever
// texture the brush got so far.
bool cPatternBrush::MakeTexture(cIDeviceDX *device)
{
    auto *const formats = static_cast<cIDeviceEnum *>(static_cast<void *>(device->GetDeviceEnumConst()));
    // ORIGINAL-BUG: no image set (mFormat null) dereferences null here, and neither texture's GetClassObject is checked.
    const uint32_t classId = mFormat->GetClassId();
    cPixelFormatRange range = formats->GetColorFormatsForDisplay(kFormatUsage, classId);
    cIPixelFormat *const chosen = FindFormatLowerBound(CopyFormats(range), mFormat);
    int32_t width = int32_t(mWidth);
    int32_t height = int32_t(mHeight);
    uint32_t levels = 1;
    nGZSceneGraph::AdjustTextureSize(device, chosen, &width, &height, &levels, false);

    cITexture *upload = nullptr;
    As<ifaces::cIGZCOM>(GetDirectorFrameworkObject())->GetClassObject(kTextureClass, kIidTexture, reinterpret_cast<void **>(&upload));
    bool made = upload->Create(device, uint32_t(width), uint32_t(height), chosen, levels, 1.0f, kUploadPool);
    if (made) {
        cIImageManip *const manip = static_cast<cIImageManip *>(GetImageManipCached());
        made = manip->LoadSurfaceFromMemory(mBitmap.data(), mFormat, mWidth, mHeight, mPitch, upload->GetTargetSurface(0));
    }
    if (made) {
        auto *const com = As<ifaces::cIGZCOM>(GetDirectorFrameworkObject());
        if (cITexture *const old = mTexture) {
            mTexture = nullptr;
            old->Release();
        }
        com->GetClassObject(kTextureClass, kIidTexture, reinterpret_cast<void **>(&mTexture));
        made = mTexture->Create(device, uint32_t(width), uint32_t(height), chosen, levels, 1.0f, kDrawPool);
    }
    if (made) {
        cIImageSurface *const target = mTexture->GetTargetSurface(0);
        target->Copy(upload->GetTargetSurface(0));
        SetAngle(mAngle);
        SetOffset(mOffsetX, mOffsetY);
    }
    if (upload) upload->Release();
    nGZEvent::ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&range));
    return made;
}

uint32_t cPatternBrush::GetStateBlock(cIDraw2D *draw2d, cIDeviceDX *device)
{
    cIDeviceState *const state = device->GetStateConst();
    if ((!mTexture || !mTexture->IsCreated()) && !MakeTexture(device)) return 0;
    if (mStateBlock && mStateBlock->IsCreated()) return uint32_t(uintptr_t(mStateBlock));

    draw2d->ApplyDrawState();
    auto filter = Command<cTextureFilterState>(kTextureFilterId, GameAddress<void>(cTextureFilterState::kVTable));
    filter.mMinFilter = 1;
    filter.mMagFilter = 1;
    filter.mMaxAnisotropy = 1;
    state->ApplyStateCommand(uint32_t(uintptr_t(&filter)));
    auto mip = Command<cTextureMipFilterState>(kTextureMipFilterId, GameAddress<void>(cTextureMipFilterState::kVTable));
    state->ApplyStateCommand(uint32_t(uintptr_t(&mip)));
    auto blend = Command<cTextureBlendState>(kTextureBlendId, GameAddress<void>(cTextureBlendState::kVTable));
    blend.mColorArg1 = 1;
    blend.mColorArg2 = 2;
    blend.mColorArg0 = 3;
    blend.mAlphaArg1 = 1;
    blend.mAlphaArg2 = 2;
    blend.mAlphaArg0 = 3;
    state->ApplyStateCommand(uint32_t(uintptr_t(&blend)));
    state->SetTexture(0, mTexture);
    if (cIDeviceResource *const old = mStateBlock) {
        mStateBlock = nullptr;
        old->Release();
    }
    state->CreateStateBlock(kStateBlockKind, uint32_t(uintptr_t(&mStateBlock)));
    return uint32_t(uintptr_t(mStateBlock));
}

void cPatternBrush::Draw(cIDraw2D *draw2d, uint32_t, uint32_t count, const float *points, const float *)
{
    const int32_t n = int32_t(count);
    float *positions;
    uint32_t *colours;
    float *uvs;
    uint16_t *indices;
    if (!draw2d->LockBatch(count, uint32_t((n - 2) * 3), &positions, &colours, &uvs, &indices)) return;
    for (int32_t i = 0; i < n; ++i) {
        const float x = points[i * 2];
        const float y = points[i * 2 + 1];
        *colours = 0;
        colours += kStride;
        const float u = (x * mMatrix[0] + y * mMatrix[1]) + mTextureOffsetX;
        const float v = (y * mMatrix[3] + x * mMatrix[2]) + mTextureOffsetY;
        uvs[0] = u;
        uvs[1] = v;
        uvs += kStride;
        positions[0] = x;
        positions[1] = y;
        positions += kStride;
    }
    WriteFan(indices, n - 2);
    draw2d->DrawBatch();
}

void cPatternBrush::SetImage(const void *bits, cIPixelFormat *format, uint32_t width, uint32_t height, uint32_t pitch)
{
    ResetCache();
    mFormat = format;
    mWidth = width;
    mHeight = height;
    mPitch = pitch;
    mBitmap.resize(height * pitch);
    std::memcpy(mBitmap.data(), bits, mPitch * mHeight);
}

void cPatternBrush::ResetCache()
{
    mBitmap.clear();
    if (cITexture *const texture = mTexture) {
        mTexture = nullptr;
        texture->Release();
    }
    if (cIDeviceResource *const block = mStateBlock) {
        mStateBlock = nullptr;
        block->Release();
    }
}

void cPatternBrush::SetAngle(float angle)
{
    mAngle = angle;
    if (!mTexture || !mTexture->IsCreated()) return;
    float rotation[4];
    Rotation(rotation, angle);
    const float heightScale = 1.0f / float(int32_t(mTexture->GetHeight(0)));
    const float widthScale = 1.0f / float(int32_t(mTexture->GetWidth(0)));
    float scale[4];
    Scaling(scale, widthScale, heightScale);
    Multiply(mMatrix, scale, rotation);
}

void cPatternBrush::SetOffset(float x, float y)
{
    mOffsetX = x;
    mOffsetY = y;
    if (!mTexture || !mTexture->IsCreated()) return;
    cITexture *const texture = mTexture;
    const double wrappedX = std::fmod(double(mOffsetX), double(int32_t(texture->GetWidth(0))));
    mTextureOffsetX = float(wrappedX / double(float(int32_t(texture->GetWidth(0)))));
    const double wrappedY = std::fmod(double(mOffsetY), double(int32_t(texture->GetHeight(0))));
    mTextureOffsetY = float(wrappedY / double(float(int32_t(texture->GetHeight(0)))));
}

float cPatternBrush::GetAngle() { return mAngle; }

float *cPatternBrush::GetOffset(float *out)
{
    out[0] = mOffsetX;
    out[1] = mOffsetY;
    return out;
}

cPatternBrush::~cPatternBrush()
{
    if (mStateBlock) mStateBlock->Release();
    if (mTexture) mTexture->Release();
}

} // namespace nGZGraphic4
