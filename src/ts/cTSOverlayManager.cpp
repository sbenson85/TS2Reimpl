#include "ts/cTSOverlayManager.h"

#include "compgeom/ConvexMath.h"
#include "compgeom/Matrix4.h"
#include "compgeom/Rigid.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZCanvas.h"
#include "gz/cIGZString.h"
#include "rz/Singleton_20ddfb.h"
#include "sg/cTextureImage.h"

#include <cmath>
#include <cstring>
#include <new>

TS2_OWNED_STATIC(nGZGraphic4::cGZMessagePool, 107d9a8);
TS2_OWNED_STATIC(nGZGraphic4::cGZMessagePool, 107d9b8);

namespace {

constexpr float kPi = 3.1415927f;

using GetSystemServiceFn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
constexpr uint32_t kCommonSystemClass = 0x8af76dc8, kCommonSystemIid = 0x4af76d8c;

template <class To, class From> To Bits(From value)
{
    To out;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

// 0xbdaa10: the first two rows scaled (the second negated) and moved by the last row.
// Operands as the original has them: with NaNs in the matrix the first one's wins, and the last column's sum is the other way.
void MapRows(float *m, float a, float b, float c, float d)
{
    using namespace nGZCompGeom::convex;
    for (int i = 0; i < 3; ++i) m[i] = AddS(MulS(m[12 + i], c), MulS(a, m[i]));
    m[3] = AddS(MulS(a, m[3]), MulS(m[15], c));
    for (int i = 0; i < 4; ++i) m[4 + i] = SubS(MulS(m[12 + i], d), MulS(b, m[4 + i]));
}

void MapToTexture(float *m) { MapRows(m, 0.5f, 0.5f, 0.5f, 0.5f); }           // 0xbda960
void MapToQuarterTexture(float *m) { MapRows(m, 0.25f, 0.25f, 0.5f, 0.5f); }  // 0xbda920

// 0xbda990: onto the window from (x0, y0) to (x1, y1).
void MapToWindow(float *m, float x0, float y0, float x1, float y1)
{
    const float halfY = (y1 - y0) * 0.5f;
    const float halfX = (x1 - x0) * 0.5f;
    MapRows(m, halfX, halfY, halfX + x0, halfY + y0);
}

// 0xbdaaf0: a decal's texture matrix from its projection.
void ScaleToQuarter(float *m)
{
    for (int i = 0; i < 4; ++i) {
        m[i] = m[i] * 0.5f + m[12 + i] * 0.25f;
        m[4 + i] = m[4 + i] * 0.5f + m[12 + i] * 0.25f;
    }
}

// The rounding of 0xbdabd0: the float's bits at 3 * 2^22 hold the integer; the exact case is put back on its side.
int32_t CellBelow(float v)
{
    const int32_t bits = Bits<int32_t>((v - 0.5f) + 12582912.0f);
    const int32_t up = bits - 0x4b3fffff;
    return static_cast<float>(up) == v ? up : bits - 0x4b400000;
}

int32_t CellAbove(float v)
{
    const int32_t bits = Bits<int32_t>(v + 0.5f + 12582912.0f);
    const int32_t down = bits - 0x4b400001;
    return static_cast<float>(down) == v ? down : bits - 0x4b400000;
}

// 0..1 to 0..255 by the bits at 49152, clamped both ways.
uint32_t ColorByte(float value)
{
    const int32_t scaled = Bits<int32_t>(value + 49152.0f) - 0x47400000;
    return (~(scaled >> 31) & scaled | (0xff - scaled) >> 31 & 0xff) & 0xff;
}

void SetRecordActive(cTSOverlayTable *table, int32_t index, bool active)
{
    OriginalFunction<void(__thiscall *)(void *, int32_t, bool), 0x3afe70>()(table, index, active);
}

// 0xbdb5d0: an overlay's draw id comes from the common system the first time.
void EnsureDrawId(cTSOverlay *overlay)
{
    if (!overlay || overlay->mDrawId) return;
    void *common = nullptr;
    if (void *const framework = GetServiceObject())
        VirtualCall<GetSystemServiceFn, 5>(framework, kCommonSystemClass, kCommonSystemIid, &common);
    overlay->mDrawId = VirtualCall<uint32_t(__thiscall *)(void *), 7>(common);
    VirtualCall<uint32_t(__thiscall *)(void *), 2>(common);
}

} // namespace

// cTSOverlay

void *cTSOverlay::operator new(size_t) { return ts2_rva_107d9a8.Allocate(); }
void cTSOverlay::operator delete(void *memory)
{
    if (memory) ts2_rva_107d9a8.Free(memory);
}

cTSOverlay::cTSOverlay() : mFlags(kVisible), mColor{1.0f, 1.0f, 1.0f}, mNode{}
{
    static constexpr float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::memcpy(mMatrix, kIdentity, sizeof(mMatrix));
    std::memcpy(mTextureMatrix, kIdentity, sizeof(mTextureMatrix));
}

cTSOverlay::~cTSOverlay() = default;

bool cTSOverlay::QueryInterface(uint32_t riid, void **out) { return rz::cRZUnknown::QueryInterface(riid, out); }
uint32_t cTSOverlay::AddRef() { return rz::cRZUnknown::AddRef(); }
uint32_t cTSOverlay::Release() { return rz::cRZUnknown::Release(); }

void cTSOverlay::SetVisible(bool value)
{
    if (static_cast<bool>(mFlags & kVisible) != value) mFlags = (value ? mFlags | kVisible : mFlags & ~kVisible) | kChanged;
}

void cTSOverlay::SetAlpha(float alpha)
{
    if (mAlpha != alpha && (alpha == 0.0f || alpha == 1.0f || 0.05f < std::fabs(alpha - mAlpha))) {
        mAlpha = alpha;
        MarkChanged();
    }
}

void cTSOverlay::SetColor(const fx::Vector3 *color)
{
    mColor = *color;
    MarkChanged();
}

void cTSOverlay::SetDraw(uint32_t draw)
{
    if (mDraw != draw) {
        mDraw = static_cast<uint8_t>(draw);
        MarkChanged();
    }
}

void cTSOverlay::SetTexture(const char *name)
{
    nGZSceneGraph::cTextureHandle handle(name);
    if (handle.mId != mTexture.mId) {
        mTexture = handle;
        MarkChanged();
    }
}

void cTSOverlay::SetLayer(uint16_t layer) { mLayer = layer; }

void cTSOverlay::PlaceWindowed(const float *rect, const float *matrix, const float *windowMin, const float *windowMax)
{
    std::memcpy(mRect, rect, sizeof(mRect));
    std::memcpy(mMatrix, matrix, sizeof(mMatrix));
    MapToWindow(mMatrix, windowMin[0], windowMin[1], windowMax[0], windowMax[1]);
    std::memcpy(mTextureMatrix, matrix, sizeof(mTextureMatrix));
    MapToQuarterTexture(mTextureMatrix);
}

void cTSOverlay::Place(const float *rect, const float *matrix)
{
    std::memcpy(mRect, rect, sizeof(mRect));
    std::memcpy(mMatrix, matrix, sizeof(mMatrix));
    MapToTexture(mMatrix);
    std::memcpy(mTextureMatrix, matrix, sizeof(mTextureMatrix));
    MapToQuarterTexture(mTextureMatrix);
}

void cTSOverlay::SetWindowedPlacement(const float *rect, const float *matrix, const float *windowMin, const float *windowMax,
                                      bool flagged)
{
    MarkChanged();
    PlaceWindowed(rect, matrix, windowMin, windowMax);
    mFlags = flagged ? mFlags | kRecordFlag : mFlags & ~kRecordFlag;
}

void cTSOverlay::SetPlacement(const float *rect, const float *matrix, bool flagged)
{
    MarkChanged();
    Place(rect, matrix);
    mFlags = flagged ? mFlags | kRecordFlag : mFlags & ~kRecordFlag;
}

bool cTSOverlay::IsVisible() { return mFlags & kVisible; }
float cTSOverlay::GetAlpha() { return mAlpha; }
fx::Vector3 *cTSOverlay::GetColor(fx::Vector3 *color)
{
    *color = mColor;
    return color;
}
uint32_t cTSOverlay::GetDraw() { return mDraw; }
uint16_t cTSOverlay::GetLayer() { return mLayer; }

// cTSDecal

void *cTSDecal::operator new(size_t) { return ts2_rva_107d9b8.Allocate(); }
void cTSDecal::operator delete(void *memory)
{
    if (memory) ts2_rva_107d9b8.Free(memory);
}

cTSDecal::cTSDecal() : mParams{{0.0f, 0.0f, 0.0f}, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0}, mTextureAspect(1.0f) {}
cTSDecal::~cTSDecal() = default;

bool cTSDecal::QueryInterface(uint32_t riid, void **out) { return rz::cRZUnknown::QueryInterface(riid, out); }
uint32_t cTSDecal::AddRef() { return rz::cRZUnknown::AddRef(); }
uint32_t cTSDecal::Release() { return rz::cRZUnknown::Release(); }

namespace {

// The distance between two floats in steps of the last place, for the three axes of SetPosition.
bool CloseEnough(float next, float stored)
{
    const uint32_t a = Bits<uint32_t>(next), b = Bits<uint32_t>(stored);
    const uint32_t signA = static_cast<uint32_t>(static_cast<int32_t>(a) >> 31);
    const uint32_t signB = static_cast<uint32_t>(static_cast<int32_t>(b) >> 31);
    const uint32_t distance = (((signA >> 1) ^ a) - ((signB >> 1) ^ b)) + signB - signA;
    return ((static_cast<uint32_t>(static_cast<int32_t>(distance) >> 26) ^ distance) & 0xffffffe0u) == 0;
}

} // namespace

void cTSDecal::SetPosition(const fx::Vector3 *position)
{
    if (CloseEnough(position->x, mParams.mPosition.x) && CloseEnough(position->y, mParams.mPosition.y) &&
        CloseEnough(position->z, mParams.mPosition.z))
        return;
    mParams.mPosition = *position;
    MarkChanged();
}

void cTSDecal::SetParams(const cTSDecalParams *p)
{
    mParams = *p;
    MarkChanged();
}

void cTSDecal::GetParams(cTSDecalParams *p) { *p = mParams; }

cITSOverlay *cTSDecal::GetOverlay() { return static_cast<cTSOverlay *>(this); }

// cTSOverlayTable

cTSOverlayRecord *cTSOverlayTable::Acquire(uint32_t key)
{
    const int32_t count = static_cast<int32_t>(mRecords.size());
    for (int32_t i = 0; i < count; ++i) {
        if (mRecords[i].mKey == key) {
            SetRecordActive(this, i, true);
            return &mRecords[i];
        }
    }
    int32_t index;
    if (mFree.empty()) {
        mRecords.push_back(cTSOverlayRecord{});
        index = static_cast<int32_t>(mRecords.size()) - 1;
    } else {
        index = static_cast<int32_t>(mFree.back());
        mFree.pop_back();
    }
    cTSOverlayRecord *const record = &mRecords[index];
    record->mActive = 1;
    record->mKey = key;
    SetRecordActive(this, index, true);
    return record;
}

// cTSOverlayManager

cTSOverlayManager::cTSOverlayManager() = default;

cTSOverlayManager::~cTSOverlayManager() = default;

cTSOverlayManager *__cdecl cTSOverlayManager::Create() { return new (std::nothrow) cTSOverlayManager; }

__declspec(noinline) cTSOverlayManager *__fastcall cTSOverlayManager::Construct(void *memory, void *)
{
    return ::new (memory) cTSOverlayManager;
}

bool cTSOverlayManager::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kIid) {
        *out = static_cast<cITSOverlayManager *>(this);
        AddRef();
        return true;
    }
    return rz::cRZUnknown::QueryInterface(riid, out);
}

uint32_t cTSOverlayManager::AddRef() { return rz::cRZUnknown::AddRef(); }
uint32_t cTSOverlayManager::Release() { return rz::cRZUnknown::Release(); }

bool cTSOverlayManager::DoMessage(void *) { return false; }

bool cTSOverlayManager::Init()
{
    if (mInitialised) return false;
    mInitialised = true;
    mChanged = false;
    SetGridSize(1.0f, 0x40, 0x40);
    mOverlayCount = 0;
    mDecalCount = 0;
    return true;
}

bool cTSOverlayManager::Shutdown()
{
    if (mInitialised) {
        mInitialised = false;
        mChanged = false;
        mOverlays.clear();
    }
    return true;
}

void cTSOverlayManager::GetStatus(cIGZString *out) { out->Sprintf("%d decals, %d overlays", mDecalCount, mOverlayCount); }

void cTSOverlayManager::SetGrid(float originX, float originY, float cellSize, int32_t width, int32_t height)
{
    mOriginX = originX;
    mOriginY = originY;
    mWidth = width;
    mHeight = height;
    mCellSize = cellSize;
    mInverseCellSize = 1.0f / cellSize;
    mCellMinX = 0;
    mCellMinY = 0;
    mCellMaxX = width;
    mCellMaxY = height;
}

void cTSOverlayManager::SetGridSize(float cellSize, int32_t width, int32_t height) { SetGrid(0.0f, 0.0f, cellSize, width, height); }

void cTSOverlayManager::SetGround(cITSOverlayGround *ground) { mGround.Assign(ground); }

void cTSOverlayManager::ComputeCells(const float *rect, int32_t *cells) const
{
    int32_t x0 = CellBelow((rect[0] - mOriginX) * mInverseCellSize);
    int32_t y0 = CellBelow((rect[2] - mOriginY) * mInverseCellSize);
    int32_t x1 = CellAbove((rect[1] - mOriginX) * mInverseCellSize);
    int32_t y1 = CellAbove((rect[3] - mOriginY) * mInverseCellSize);
    if (x0 <= mCellMaxX && mCellMinX <= x1 && y0 <= mCellMaxY && mCellMinY <= y1) {
        cells[0] = x0 < mCellMinX ? mCellMinX : x0;
        cells[1] = y0 < mCellMinY ? mCellMinY : y0;
        cells[2] = x1 > mCellMaxX ? mCellMaxX : x1;
        cells[3] = y1 > mCellMaxY ? mCellMaxY : y1;
    } else {
        cells[0] = cells[1] = cells[2] = cells[3] = 0;
    }
}

void cTSOverlayManager::AddOverlay(cITSOverlay *face)
{
    cTSOverlay *const overlay = static_cast<cTSOverlay *>(face);
    if (!overlay || (overlay->mFlags & cTSOverlay::kInManager)) return;
    EnsureDrawId(overlay);
    ComputeCells(overlay->mRect, overlay->mCells);
    overlay->mFlags |= cTSOverlay::kInManager;
    mOverlays.push_back(fx::CountedRef<cTSOverlay>(overlay));
    overlay->mNode = std::prev(mOverlays.end());
    mChanged = true;
}

void cTSOverlayManager::RemoveOverlay(cITSOverlay *face)
{
    cTSOverlay *const overlay = static_cast<cTSOverlay *>(face);
    if (!(overlay->mFlags & cTSOverlay::kInManager)) return;
    mOverlays.erase(overlay->mNode);
    // ORIGINAL-BUG: when the list's was the last reference the overlay is gone here, and this writes its old pool element.
    overlay->mFlags &= ~cTSOverlay::kInManager;
    mChanged = true;
}

cITSDecal *cTSOverlayManager::CreateDecal(const char *texture, const fx::Vector3 *position, float size, float rotation)
{
    cTSDecal *const decal = new cTSDecal;
    if (!decal) return nullptr;
    decal->mParams.mPosition = *position;
    decal->mParams.mSize = size;
    decal->mParams.mRotation = rotation;
    decal->mTexture = nGZSceneGraph::cTextureHandle(texture);
    decal->mTextureAspect = 1.0f;
    if (auto *const image = static_cast<nGZSceneGraph::cITexture *>(decal->mTexture.Get())) {
        void *const device = image->GetDeviceTexture(0);
        if (device && VirtualCall<bool(__thiscall *)(void *), 6>(device)) {
            const int32_t height = VirtualCall<int32_t(__thiscall *)(void *, uint32_t), 18>(device, 0);
            const int32_t width = VirtualCall<int32_t(__thiscall *)(void *, uint32_t), 17>(device, 0);
            decal->mTextureAspect = static_cast<float>(height) / static_cast<float>(width);
        }
    }
    decal->mFlags |= cTSOverlay::kIsDecal;
    UpdateProjection(decal);
    AddOverlay(static_cast<cTSOverlay *>(decal));
    return decal;
}

cITSOverlay *cTSOverlayManager::CreateOverlay(const char *texture, const float *rect, const float *matrix)
{
    cTSOverlay *const overlay = new cTSOverlay;
    if (!overlay) return nullptr;
    EnsureDrawId(overlay);
    overlay->mTexture = nGZSceneGraph::cTextureHandle(texture);
    overlay->Place(rect, matrix);
    AddOverlay(overlay);
    return overlay;
}

cITSOverlay *cTSOverlayManager::CreateWindowedOverlay(const char *texture, const float *rect, const float *matrix,
                                                      const float *windowMin, const float *windowMax)
{
    cTSOverlay *const overlay = new cTSOverlay;
    if (!overlay) return nullptr;
    EnsureDrawId(overlay);
    overlay->mTexture = nGZSceneGraph::cTextureHandle(texture);
    overlay->PlaceWindowed(rect, matrix, windowMin, windowMax);
    AddOverlay(overlay);
    return overlay;
}

bool cTSOverlayManager::Update(cTSOverlayTable *table)
{
    mVisible.clear();
    bool changed = mChanged;
    mOverlayCount = 0;
    mDecalCount = 0;
    for (const fx::CountedRef<cTSOverlay> &entry : mOverlays) {
        cTSOverlay *const overlay = entry.Get();
        const uint32_t flags = overlay->mFlags;
        if (flags & cTSOverlay::kChanged) changed = true;
        if (!(flags & cTSOverlay::kVisible)) {
            overlay->mFlags = flags & ~cTSOverlay::kChanged;
            continue;
        }
        if (flags & cTSOverlay::kChanged) {
            if (flags & cTSOverlay::kIsDecal) {
                UpdateProjection(static_cast<cTSDecal *>(overlay));
                ++mDecalCount;
            }
            ComputeCells(overlay->mRect, overlay->mCells);
            ++mOverlayCount;
        }
        mVisible.push_back(overlay);
    }

    if (changed && table) {
        table->mFree.resize(table->mRecords.size());
        uint32_t index = 0;
        for (uint32_t &slot : table->mFree) {
            table->mRecords[index].mActive = 0;
            slot = index++;
        }
        for (cTSOverlay *const overlay : mVisible) {
            cTSOverlayRecord *const record = table->Acquire(overlay->mDrawId);
            std::memcpy(record->mCells, overlay->mCells, sizeof(record->mCells));
            std::memcpy(record->mRect, overlay->mRect, sizeof(record->mRect));
            std::memcpy(record->mMatrix, overlay->mMatrix, sizeof(record->mMatrix));
            std::memcpy(record->mTextureMatrix, overlay->mTextureMatrix, sizeof(record->mTextureMatrix));
            record->mColor = ColorByte(overlay->mAlpha) << 24 | ColorByte(overlay->mColor.x) << 16 |
                             ColorByte(overlay->mColor.y) << 8 | ColorByte(overlay->mColor.z);
            record->mTexture = overlay->mTexture.mId;
            record->mDraw = overlay->mDraw;
            record->mFlags = (overlay->mFlags & cTSOverlay::kRecordFlag) ? 1 : 0;
            if (overlay->mFlags & cTSOverlay::kChanged) {
                record->mFlags |= 2;
                overlay->mFlags &= ~cTSOverlay::kChanged;
            }
            record->mLayer = overlay->GetLayer();
        }
        mChanged = false; // only here: a pass without a table leaves it for the next
    }
    return changed;
}

void cTSOverlayManager::UpdateProjection(cTSDecal *decal)
{
    const cTSDecalParams &p = decal->mParams;
    const float textureAspect = decal->mTextureAspect * p.mAspect;
    const float size = p.mSize;
    float halfWidth = size * 0.5f;
    const float inverse = 1.0f / size;
    float halfHeight = size * textureAspect * 0.5f;
    const float repeatU = p.mRepeatU;
    const float repeatV = p.mRepeatV;

    fx::Vector3 u{1.0f, 0.0f, 0.0f}, v{0.0f, 1.0f, 0.0f};
    float offsetU, offsetV;
    if (!mGround.Get()) {
        offsetU = halfWidth - p.mPosition.x;
        offsetV = halfHeight - p.mPosition.y;
    } else {
        fx::Vector3 position = p.mPosition;
        mGround->SnapPosition(&position);
        fx::Vector3 n;
        if (mGround->GetNormal(&position, &n)) {
            if (std::fabs(n.x) > std::fabs(n.y)) {
                const float t = -n.x;
                u = {n.z, -0.0f, t};
                v = {t * n.y - n.z * -0.0f, n.z * n.z - n.x * t, n.x * -0.0f - n.z * n.y};
            } else {
                const float t = -n.y;
                v = {0.0f, n.z, t};
                u = {n.z * n.z - t * n.y, n.x * t - n.z * 0.0f, n.y * 0.0f - n.x * n.z};
            }
        }
        const fx::Vector3 negated{-position.x, -position.y, -position.z};
        offsetU = static_cast<float>(nGZCompGeom::Dot87(&u.x, &negated.x) + static_cast<double>(halfWidth));
        offsetV = static_cast<float>(nGZCompGeom::Dot87(&v.x, &negated.x) + static_cast<double>(halfHeight));
    }

    float m[16] = {u.x * inverse * repeatU,
                   u.y * inverse * repeatU,
                   u.z * inverse * repeatU,
                   offsetU * inverse * repeatU + p.mOffsetU,
                   0.0f,
                   0.0f,
                   0.0f,
                   0.0f,
                   0.0f,
                   0.0f,
                   1.0f,
                   0.0f,
                   0.0f,
                   0.0f,
                   0.0f,
                   1.0f};
    const float inverseV = inverse / textureAspect;
    m[4] = inverseV * v.x * repeatV;
    m[5] = inverseV * v.y * repeatV;
    m[6] = inverseV * v.z * repeatV;
    m[7] = inverseV * offsetV * repeatV + p.mOffsetV;

    if (p.mRotation != 0.0f) {
        const float angle = p.mRotation * 6.2831855f;
        const float c = static_cast<float>(std::cos(static_cast<double>(angle)));
        const float s = static_cast<float>(std::sin(static_cast<double>(angle)));
        const float px = p.mPosition.x, py = p.mPosition.y;
        using nGZCompGeom::convex::AddS, nGZCompGeom::convex::MulS, nGZCompGeom::convex::SubS;
        const float t = 1.0f - c;
        // In the original's operand order: with an infinite rotation and a NaN position both sides of a product are NaN.
        const float rotation[16] = {c,  s, 0.0f, SubS(MulS(t, px), MulS(py, s)), //
                                    -s, c, 0.0f, AddS(MulS(t, py), MulS(px, s)), //
                                    0.0f, 0.0f, 1.0f, 0.0f,                //
                                    0.0f, 0.0f, 0.0f, 1.0f};
        float out[16];
        std::memcpy(m, nGZCompGeom::Matrix4Multiply(out, rotation, m), sizeof(m));
        if (repeatU == 1.0f && repeatV == 1.0f) {
            halfHeight *= std::fabs(s) / textureAspect + std::fabs(c);
            halfWidth *= std::fabs(s) * textureAspect + std::fabs(c);
        }
    }

    decal->mRect[0] = p.mPosition.x - halfWidth;
    decal->mRect[1] = p.mPosition.x + halfWidth;
    decal->mRect[2] = p.mPosition.y - halfHeight;
    decal->mRect[3] = p.mPosition.y + halfHeight;
    std::memcpy(decal->mMatrix, m, sizeof(m));
    std::memcpy(decal->mTextureMatrix, m, sizeof(m));
    ScaleToQuarter(decal->mTextureMatrix);
    decal->mFlags = (repeatU == 1.0f && repeatV == 1.0f) ? decal->mFlags & ~cTSOverlay::kRepeating
                                                         : decal->mFlags | cTSOverlay::kRepeating;
}

// cTSTestMap

cTSTestMap *__cdecl cTSTestMap::Create() { return new (std::nothrow) cTSTestMap; }

bool cTSTestMap::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kIid) {
        *out = static_cast<cITSEffectMap *>(this);
        AddRef();
        return true;
    }
    return rz::cRZUnknown::QueryInterface(riid, out);
}

uint32_t cTSTestMap::AddRef() { return rz::cRZUnknown::AddRef(); }
uint32_t cTSTestMap::Release() { return rz::cRZUnknown::Release(); }

void cTSTestMap::GetBounds(float *minX, float *minY, float *maxX, float *maxY)
{
    *minX = -4.0f;
    *minY = -4.0f;
    *maxX = 4.0f;
    *maxY = 4.0f;
}

bool cTSTestMap::Contains(float x, float y) { return 0.0f <= x && x < 8.0f && 0.0f <= y && y < 8.0f; }

float cTSTestMap::GetHeight(float x, float y)
{
    const float waveY = static_cast<float>(std::sin(static_cast<double>(y * kPi * 0.5f)));
    const float waveX = static_cast<float>(std::sin(static_cast<double>(x * kPi)));
    return (waveY + 1.0f) * 0.1f + (waveX + 1.0f) * 0.3f;
}

fx::Vector3 *cTSTestMap::GetNormal(fx::Vector3 *normal, float x, float y)
{
    const float a = static_cast<float>(std::cos(static_cast<double>(x * kPi))) * -0.3f * kPi;
    const float b = static_cast<float>(std::cos(static_cast<double>(y * kPi * 0.5f))) * -0.1f * kPi * 0.5f;
    const float length = static_cast<float>(std::sqrt(static_cast<double>(a * a + 1.0f + b * b)));
    normal->x = a / length;
    normal->y = b / length;
    normal->z = 1.0f / length;
    return normal;
}

float cTSTestMap::GetScale() { return 0.2f; }
