// nGZSceneGraph::cViewerRefNodeImpl's view maths (lane TZ, scenegraph N3r): the target's aspect, the entries'
// projections, the viewport matrix and the point transforms. Every float expression keeps the original's operand
// order (single precision, each step rounded); unsigned sizes become floats through double, as the original converts
// them. See sg/cViewerRefNodeImpl.h.
#include "sg/cViewerRefNodeImpl.h"

#include "compgeom/CompGeom.h"
#include "compgeom/ConvexMath.h"
#include "compgeom/Matrix4.h"
#include "gz/cGZAny.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZPropertySet.h" // cIGZPropertySetString, GZServiceString
#include "rz/RZAutoRef.h"
#include "rz/RZFrameworkHelpers.h"

#include <cmath>
#include <cstring>

namespace nGZSceneGraph {

namespace {

constexpr float kHalf = 0.5f;
constexpr float kOne = 1.0f;

float UnsignedToFloat(uint32_t value) { return static_cast<float>(static_cast<double>(value)); }

// The configuration service's "usePixelAspectRatio" (on when the service or the property is missing). The set is
// asked only when the service answered.
bool UsePixelAspectRatio()
{
    RZAutoRef settings{nullptr};
    auto *const framework = reinterpret_cast<cIGZFrameWork *>(static_cast<uintptr_t>(RZGetGlobal10C9378()));
    if (framework != nullptr)
        framework->GetSystemService(cViewerRefNodeImpl::kSettingsService, cViewerRefNodeImpl::kSettingsIid,
                                    &settings.mObject);
    gz::AnyBoolean value(1);
    if (settings.mObject != nullptr) {
        GZServiceString key("usePixelAspectRatio");
        static_cast<cIGZPropertySetString *>(settings.mObject)->GetPropertyValue(&key, &value);
    }
    settings.ReleaseObject();
    return value.mValue != 0;
}

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::MulS;

// Row r of a 4x4 (row-major) applied to the point (x, y, z, 1). The original sums y's term first, then x's, z's and
// the constant, each sum and product keeping its first operand's NaN.
float Row(const float *m, int r, float x, float y, float z)
{
    return AddS(AddS(AddS(MulS(y, m[r + 1]), MulS(x, m[r])), MulS(z, m[r + 2])), m[r + 3]);
}

// The point through a 4x4 with the perspective divide (the fourth row's result).
float *Project(float *out, const float *m, const float *point)
{
    const float x = point[0], y = point[1], z = point[2];
    const float inverse = kOne / Row(m, 12, x, y, z);
    out[0] = MulS(Row(m, 0, x, y, z), inverse);
    out[1] = MulS(Row(m, 4, x, y, z), inverse);
    out[2] = MulS(Row(m, 8, x, y, z), inverse);
    return out;
}

// The part of the target the entry's viewport covers: x0, y0, x1, y1 clamped to [0, 1] (a NaN gives the bound).
struct Clip {
    float x0, y0, x1, y1;
    explicit Clip(const cViewerRefEntry &entry)
    {
        const float *const v = entry.mViewport;
        x0 = v[0] > 0.0f ? v[0] : 0.0f;
        y0 = v[1] > 0.0f ? v[1] : 0.0f;
        const float right = v[0] + v[2];
        const float bottom = v[1] + v[3];
        x1 = right < kOne ? right : kOne;
        y1 = bottom < kOne ? bottom : kOne;
    }
};

nGZCompGeom::RigidTransform3 *AsRigid(cRigidTransform *t) { return reinterpret_cast<nGZCompGeom::RigidTransform3 *>(t); }

} // namespace

void cViewerRefNodeImpl::RefreshAspect()
{
    cViewerTargetSize size;
    GetTargetSize(&size);
    mTargetWidth = size.mWidth;
    mTargetHeight = size.mHeight;
    mTargetScale = size.mScale;
    const bool pixelAspect = UsePixelAspectRatio();
    const float width = UnsignedToFloat(mTargetWidth);
    float height = UnsignedToFloat(mTargetHeight);
    if (pixelAspect) height = height * mTargetScale;
    const float aspect = width / height;
    if (!(mAspect == aspect)) {
        mAspect = aspect;
        for (cViewerRefEntry &entry : mEntries) entry.mDirty = 1;
    }
}

cViewerRefEntry *cViewerRefNodeImpl::GetEntry(uint32_t index)
{
    cViewerRefEntry *const entry = mEntries.data() + index;
    cViewerTargetSize size;
    GetTargetSize(&size);
    if (size.mWidth != mTargetWidth || size.mHeight != mTargetHeight || !(size.mScale == mTargetScale))
        RefreshAspect();
    if (entry->mDirty != 0) {
        entry->mAspect = mAspect;
        entry->mDirty = 0;
        UpdateProjection(entry);
    }
    return entry;
}

void cViewerRefNodeImpl::UpdateProjection(cViewerRefEntry *entry)
{
    if (entry->mType == cViewerRefEntry::kExplicit) return;
    float *const p = entry->mProjection;
    std::memset(p, 0, sizeof entry->mProjection);
    const float subW = entry->mSubrect[2], subH = entry->mSubrect[3];
    if (subW == 0.0f || subH == 0.0f) return;
    const Clip clip(*entry);
    const float dx = clip.x1 - clip.x0, dy = clip.y1 - clip.y0;
    if (dx == 0.0f || dy == 0.0f) return;

    const float vw = entry->mViewport[2], vh = entry->mViewport[3];
    // The scale and centre of the drawn part of the viewport.
    const float sx = vw / dx;
    const float sy = vh / dy;
    const float halfDx = dx * kHalf, halfDy = dy * kHalf;
    const float cx = (((vw * kHalf + entry->mViewport[0]) - (halfDx + clip.x0)) + entry->mOffset[0]) / halfDx;
    const float cy = (((vh * kHalf + entry->mViewport[1]) - (halfDy + clip.y0)) + entry->mOffset[1]) / halfDy;

    cViewerTargetSize unused;
    GetTargetSize(&unused);
    const bool pixelAspect = UsePixelAspectRatio();
    float aspect = pixelAspect ? vw / (vh * entry->mPixelAspect) : vw / vh;
    aspect = aspect * mAspect;
    if (entry->mFixedAspect != 0) aspect = vw / (vh * entry->mPixelAspect);

    const float nearClip = entry->mNear, farClip = entry->mFar;
    if (entry->mType == cViewerRefEntry::kPerspective) {
        p[14] = kOne;
        const float t = static_cast<float>(std::tan(static_cast<double>(entry->mFov * kHalf)));
        p[0] = sx / t;
        p[5] = (aspect * sy) / t;
        const float depth = farClip - nearClip;
        p[11] = -(nearClip * farClip) / depth;
        p[10] = farClip / depth;
        p[2] = cx;
        p[6] = -cy;
    } else if (entry->mType == cViewerRefEntry::kOrtho) {
        const float left = entry->mOrtho[0], right = entry->mOrtho[1];
        const float bottom = entry->mOrtho[2], top = entry->mOrtho[3];
        const float width = right - left;
        const float scaledY = aspect * sy;
        p[0] = (sx + sx) / width;
        p[5] = (scaledY + scaledY) / width;
        const float depth = farClip - nearClip;
        p[3] = cx - ((left + right) * sx) / width;
        p[10] = kOne / depth;
        p[15] = kOne;
        p[11] = -nearClip / depth;
        p[7] = ((-(bottom + top)) * sy) / (top - bottom) - cy;
    }
    // The rows' middle columns exchanged (the original's layout of the projection).
    auto swap = [p](int a, int b) {
        const float t = p[a];
        p[a] = p[b];
        p[b] = t;
    };
    swap(1, 2);
    swap(5, 6);
    swap(9, 10);
    swap(13, 14);

    // Into the sub-rectangle: x' = (x - (sub.x + ...)) / sub.w in clip terms.
    const float subX = entry->mSubrect[0], subY = entry->mSubrect[1];
    float s[16] = {};
    s[0] = kOne / subW;
    s[3] = ((kOne - (subX + subW)) - subX) / subW;
    s[5] = kOne / subH;
    s[7] = ((kOne - (subY + subH)) - subY) / subH;
    s[10] = kOne;
    s[15] = kOne;
    float product[16];
    std::memcpy(p, nGZCompGeom::Matrix4Multiply(product, p, s), sizeof product);
}

float *cViewerRefNodeImpl::GetViewport(float *out, uint32_t index)
{
    const cViewerRefEntry &entry = mEntries.data()[index];
    const Clip clip(entry);
    const float dx = clip.x1 - clip.x0, dy = clip.y1 - clip.y0;
    float m[16] = {};
    if (dx > 0.0f && dy > 0.0f) {
        cViewerTargetSize size;
        GetTargetSize(&size);
        const float width = UnsignedToFloat(size.mWidth);
        const float height = UnsignedToFloat(size.mHeight);
        m[0] = (width * dx) * kHalf;
        m[3] = (dx * kHalf + clip.x0) * width;
        m[5] = ((-dy) * height) * kHalf;
        m[7] = (dy * kHalf + clip.y0) * height;
        m[10] = entry.mDepthMax - entry.mDepthMin;
        m[11] = entry.mDepthMin;
        m[15] = kOne;
    } else {
        m[0] = m[5] = m[10] = m[15] = kOne;
    }
    std::memcpy(out, m, sizeof m);
    return out;
}

cRigidTransform *cViewerRefNodeImpl::GetView(cRigidTransform *out, cICompositionTreeNode *outer, uint32_t index)
{
    const cViewerRefEntry *const entry = GetEntry(index);
    RefreshIfStale(outer);
    const cRigidTransform world = mWorld;
    cRigidTransform view = entry->mPlacement;
    *out = *game_code::ComposeRigid(&view, &world);
    return out;
}

float *cViewerRefNodeImpl::WorldToClip(float *out, cICompositionTreeNode *outer, const float *point, uint32_t index)
{
    cViewerRefEntry *const entry = GetEntry(index);
    RefreshIfStale(outer);
    cRigidTransform world = mWorld;
    nGZCompGeom::Vec3 local = {point[0], point[1], point[2]};
    AsRigid(&world)->InverseTransformPoint(&local);
    nGZCompGeom::Vec3 eye = local;
    AsRigid(&entry->mPlacement)->InverseTransformPoint(&eye);
    const float p[3] = {eye.x, eye.y, eye.z};
    return Project(out, entry->mProjection, p);
}

float *cViewerRefNodeImpl::WorldToScreen(float *out, cICompositionTreeNode *outer, const float *point, uint32_t index)
{
    cViewerRefEntry *const entry = GetEntry(index);
    RefreshIfStale(outer);
    cRigidTransform world = mWorld;
    nGZCompGeom::Vec3 local = {point[0], point[1], point[2]};
    AsRigid(&world)->InverseTransformPoint(&local);
    nGZCompGeom::Vec3 eye = local;
    AsRigid(&entry->mPlacement)->InverseTransformPoint(&eye);
    const float *const pm = entry->mProjection;
    // The clip coordinates (no divide), then through the viewport with its divide.
    const float c[4] = {Row(pm, 0, eye.x, eye.y, eye.z), Row(pm, 4, eye.x, eye.y, eye.z),
                        Row(pm, 8, eye.x, eye.y, eye.z), Row(pm, 12, eye.x, eye.y, eye.z)};
    float viewport[16];
    const float *const v = GetViewport(viewport, index);
    auto apply = [&c, v](int r) {
        return AddS(AddS(AddS(MulS(c[1], v[r + 1]), MulS(c[0], v[r])), MulS(c[2], v[r + 2])), MulS(c[3], v[r + 3]));
    };
    const float inverse = kOne / apply(12);
    out[0] = MulS(apply(0), inverse);
    out[1] = MulS(apply(4), inverse);
    out[2] = MulS(apply(8), inverse);
    return out;
}

float *cViewerRefNodeImpl::ClipToWorld(float *out, cICompositionTreeNode *outer, const float *point, uint32_t index)
{
    const cViewerRefEntry *const entry = GetEntry(index);
    cRigidTransform view;
    GetView(&view, outer, index);
    AsRigid(&view)->Invert();
    float rigid[16], product[16], inverse[16];
    AsRigid(&view)->ToMatrix4(rigid);
    nGZCompGeom::Matrix4Multiply(product, rigid, entry->mProjection);
    return Project(out, nGZCompGeom::Matrix4Inverse(inverse, product), point);
}

float *cViewerRefNodeImpl::ScreenToWorld(float *out, cICompositionTreeNode *outer, const float *point, uint32_t index)
{
    const cViewerRefEntry *const entry = GetEntry(index);
    cRigidTransform view;
    GetView(&view, outer, index);
    AsRigid(&view)->Invert();
    float viewport[16];
    GetViewport(viewport, index);
    float rigid[16], product[16], full[16], inverse[16];
    AsRigid(&view)->ToMatrix4(rigid);
    nGZCompGeom::Matrix4Multiply(product, rigid, entry->mProjection);
    nGZCompGeom::Matrix4Multiply(full, product, viewport);
    return Project(out, nGZCompGeom::Matrix4Inverse(inverse, full), point);
}

} // namespace nGZSceneGraph
