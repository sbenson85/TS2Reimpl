// cGZDrawContext (src/gz/cGZDrawContext.h; docs/subsystems/rendering.md, R5).
#include "gz/cGZDrawContext.h"

#include <new>

#include "game/original.h"
#include "ifaces/cIGZFrameWork.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject

namespace {

// 0x373520 (__thiscall, ret 8): `*out` = a & b when they overlap (true), else all zero (false). `out` may be `a`.
bool Intersect(cGZRect *out, const cGZRect *a, const cGZRect *b)
{
    return OriginalFunction<bool(__thiscall *)(cGZRect *, const cGZRect *, const cGZRect *), 0x373520>()(out, a, b);
}

bool Valid(const cGZRect *r) { return r->left < r->right && r->top < r->bottom; }

} // namespace

cGZDrawContext::cGZDrawContext()
{
    // The origin, the clips, the clip flag, the bounds, the texture fields and the state's pad byte are left as they
    // were: the original's constructor does not write them (the pad byte is copied into mSent along with the state).
    mState.byte0 = 0;
    mState.byte1 = 0;
    mState.byte2 = 0;
    mState.word = 0;
    mState.lineWidth = 1.0f;
    mState.lineWidthRounded = 1;
    mBatchDepth = 0;
    mRefCount = 0;
    mSent = mState;
    mStateDirty = false;
    mColors[0] = 0;
    mColors[1] = 0;
    mColors[2] = 0;
    mColors[3] = 0;
}

cGZDrawContext *cGZDrawContext::Create() { return new cGZDrawContext; }

cGZDrawContext::~cGZDrawContext()
{
    if (mEngine.p) mEngine.p->Detach();
}

bool cGZDrawContext::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid != 1 && riid != kIid) return false;
    *ppvObj = static_cast<cIGZDrawContext *>(this);
    AddRef();
    return true;
}

uint32_t cGZDrawContext::AddRef() { return ++mRefCount; }

uint32_t cGZDrawContext::Release()
{
    const uint32_t count = --mRefCount;
    if (count == 0) delete this;
    return count;
}

void cGZDrawContext::PushTransform() { mTransforms.push_back({mOriginX, mOriginY, mClip}); }

void cGZDrawContext::ResetClip()
{
    mSentClip = {0, 0, 0, 0};
    mClipDirty = true;
    mClip = mBounds;
}

void cGZDrawContext::IntersectClip(const cGZRect *rect)
{
    const int32_t x = static_cast<int32_t>(mOriginX);
    const int32_t y = static_cast<int32_t>(mOriginY);
    const cGZRect moved{rect->left + x, rect->top + y, rect->right + x, rect->bottom + y};
    Intersect(&mClip, &mClip, &moved);
    mClipDirty = true;
}

void cGZDrawContext::IntersectClipAbsolute(const cGZRect *rect)
{
    Intersect(&mClip, &mClip, rect);
    mClipDirty = true;
}

void cGZDrawContext::Translate(const cGZPoint *offset)
{
    mOriginX = static_cast<float>(offset->x) + mOriginX;
    mOriginY = static_cast<float>(offset->y) + mOriginY;
}

void cGZDrawContext::SetOrigin(const cGZPoint *origin)
{
    mOriginX = static_cast<float>(origin->x);
    mOriginY = static_cast<float>(origin->y);
}

void cGZDrawContext::PopTransform()
{
    if (mTransforms.empty()) return;
    const Transform &top = mTransforms.back();
    mOriginX = top.originX;
    mOriginY = top.originY;
    mClip = top.clip;
    mTransforms.pop_back();
    mClipDirty = true;
}

void cGZDrawContext::PushState() { mStates.push_back(mState); }

void cGZDrawContext::PopState()
{
    if (mStates.empty()) return;
    const State saved = mStates.back();
    vf25(saved.byte0);
    vf24(saved.byte1);
    vf22(saved.word);
    vf23(saved.byte2);
    SetLineWidth(saved.lineWidth);
    mStates.pop_back();
}

cGZRect *cGZDrawContext::GetBounds(cGZRect *bounds)
{
    *bounds = mBounds;
    return bounds;
}

cGZRect *cGZDrawContext::GetClip(cGZRect *clip)
{
    *clip = mClip;
    return clip;
}

cGZPoint *cGZDrawContext::GetOrigin(cGZPoint *origin)
{
    origin->x = static_cast<int32_t>(mOriginX);
    origin->y = static_cast<int32_t>(mOriginY);
    return origin;
}

int32_t cGZDrawContext::GetTransformDepth() { return static_cast<int32_t>(mTransforms.size()); }

bool cGZDrawContext::HasTarget() { return mTarget.p != nullptr; }

void cGZDrawContext::SetTarget(cIGZBuffer *target)
{
    if (mTarget.p == target) return;
    if (mEngine.p) {
        mEngine.p->Detach();
        if (cIGZDrawContextEngine *const old = mEngine.p) {
            mEngine.p = nullptr;
            old->Release();
        }
    }
    if (cIGZBuffer *const old = mTarget.p; target != old) {
        if (target) target->AddRef();
        mTarget.p = target;
        if (old) old->Release();
    }
    if (!target) return;

    const cGZRect *const area = target->GetBoundsPtr();
    mOriginX = 0.0f;
    mOriginY = 0.0f;
    mClip = *area;

    // The draw system makes the engine. Its absence is not checked: the original then calls through null.
    RZAutoRef system{nullptr};
    if (void *const framework = GetServiceObject())
        static_cast<ifaces::cIGZFrameWork *>(framework)->GetSystemService(cIGZDrawSystem::kServiceId,
                                                                          cIGZDrawSystem::kIid, &system.mObject);
    cIGZDrawSystem *const drawSystem = static_cast<cIGZDrawSystem *>(system.mObject);
    if (cIGZDrawContextEngine *const old = mEngine.p) {
        mEngine.p = nullptr;
        old->Release();
    }
    drawSystem->CreateEngine(target, &mEngine.p);
    system.ReleaseObject();

    mEngine.p->SetTarget(target);
    mEngine.p->SetClip(&mClip);
    mEngine.p->SetColors(0, 4, mColors);
    mEngine.p->vf8(mSent.word);
    mEngine.p->vf9(mSent.byte2);
    mEngine.p->vf10(mSent.byte1);
    mEngine.p->vf11(mSent.byte0);
    mEngine.p->SetLineWidth(mSent.lineWidth);
    mStateDirty = false;
    cGZRect textureArea;
    mEngine.p->SetTexture(nullptr, 0.0f, 0.0f, 0.0f, 0.0f, &textureArea);
    mEngine.p->SetGradient(nullptr, 0.0f, 0.0f);
    mEngine.p->GetBounds(&mBounds);
    mClipDirty = false;
    mTextured = false;
    mTextureClipped = false;
    mSentClip = mClip;
    mBatchDepth = 0;
}

cIGZDrawContextEngine *cGZDrawContext::GetEngine() { return mEngine.p; }

void cGZDrawContext::ResetState()
{
    vf22(0);
    vf23(0);
    vf24(0);
    vf25(0);
    SetLineWidth(1.0f);
    ClearTexture();
    ClearTextureClip();
    ClearGradient();
}

void cGZDrawContext::SetColorAt(uint32_t index, uint32_t argb)
{
    mColors[index] = argb; // unchecked, as the original
    if (mEngine.p) mEngine.p->SetColors(0, 4, mColors);
}

void cGZDrawContext::SetColor(uint32_t argb) { SetColorAt(0, argb); }

void cGZDrawContext::vf22(uint32_t value)
{
    mState.word = value;
    mStateDirty = true;
}

void cGZDrawContext::vf23(uint8_t value)
{
    mState.byte2 = value;
    mStateDirty = true;
}

void cGZDrawContext::vf24(uint8_t value)
{
    mState.byte1 = value;
    mStateDirty = true;
}

void cGZDrawContext::vf25(uint8_t value)
{
    mState.byte0 = value;
    mStateDirty = true;
}

void cGZDrawContext::SetLineWidth(float width)
{
    mStateDirty = true;
    mState.lineWidth = width > 0.0f ? width : 0.0f; // maxss: NaN and -0 become +0
}

void cGZDrawContext::ClearGradient() { mEngine.p->SetGradient(nullptr, 0.0f, 0.0f); }

void cGZDrawContext::SetGradient(const float *coefficients, float x, float y)
{
    mEngine.p->SetGradient(coefficients, x, y);
}

void cGZDrawContext::ClearTexture()
{
    cGZRect area;
    mEngine.p->SetTexture(nullptr, 0.0f, 0.0f, 1.0f, 1.0f, &area);
    if (mTextured) {
        mTextured = false;
        mClipDirty = true;
    }
}

void cGZDrawContext::SetTexture(cIGZUnknown *texture, float x, float y, float sx, float sy)
{
    const bool textured = mEngine.p->SetTexture(texture, x + mOriginX, y + mOriginY, sx, sy, &mTextureArea);
    if (textured || mTextured) mClipDirty = true;
    mTextured = textured;
}

void cGZDrawContext::ClearTextureClip()
{
    mEngine.p->ClearTextureClip();
    if (mTextureClipped) {
        const bool textured = mTextured;
        mTextureClipped = false;
        if (textured) mClipDirty = true;
    }
}

void cGZDrawContext::SetTextureClip(const cGZRect *clip)
{
    if (!Valid(clip)) {
        ClearTextureClip();
        return;
    }
    mEngine.p->SetTextureClip(clip);
    if (!mTextureClipped) {
        const bool textured = mTextured;
        mTextureClipped = true;
        if (textured) mClipDirty = true;
    }
}

void cGZDrawContext::FillClip()
{
    const float points[4] = {static_cast<float>(mClip.left), static_cast<float>(mClip.top),
                             static_cast<float>(mClip.right), static_cast<float>(mClip.bottom)};
    DrawPrimitivesAbsolute(3, points, 2);
}

void cGZDrawContext::DrawPoint(int32_t x, int32_t y)
{
    const float point[2] = {static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
    DrawPrimitives(0, point, 1);
}

void cGZDrawContext::DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    const float points[4] = {static_cast<float>(x0) + 0.5f, static_cast<float>(y0) + 0.5f,
                             static_cast<float>(x1) + 0.5f, static_cast<float>(y1) + 0.5f};
    DrawPrimitives(1, points, 2);
}

void cGZDrawContext::FillRect(const cGZRect *rect)
{
    const float points[4] = {static_cast<float>(rect->left), static_cast<float>(rect->top),
                             static_cast<float>(rect->right), static_cast<float>(rect->bottom)};
    DrawPrimitives(3, points, 2);
}

void cGZDrawContext::vf37(int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    const float points[4] = {static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
                             static_cast<float>(bottom)};
    DrawPrimitives(5, points, 2);
}

void cGZDrawContext::vf38(int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    const float points[4] = {static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
                             static_cast<float>(bottom)};
    DrawPrimitives(5, points, 2);
}

void cGZDrawContext::DrawTextureStretched(cIGZUnknown *texture, const cGZRect *source, const cGZRect *target)
{
    if (!Valid(source) || !Valid(target)) return;
    SetTexture(texture, static_cast<float>(target->left - source->left),
               static_cast<float>(target->top - source->top),
               static_cast<float>(target->right - target->left) / static_cast<float>(source->right - source->left),
               static_cast<float>(target->bottom - target->top) / static_cast<float>(source->bottom - source->top));
    ClearTextureClip();
    FillRect(target);
    ClearTexture();
}

void cGZDrawContext::DrawTextureClipped(cIGZUnknown *texture, const cGZRect *clip, const cGZRect *target, int32_t x,
                                        int32_t y)
{
    if (!Valid(clip) || !Valid(target)) return;
    SetTexture(texture, static_cast<float>(x + target->left), static_cast<float>(y + target->top), 1.0f, 1.0f);
    SetTextureClip(clip);
    FillRect(target);
    ClearTextureClip();
    ClearTexture();
}

void cGZDrawContext::vf41() { mEngine.p->vf17(); }

bool cGZDrawContext::InBatch() { return mBatchDepth > 0; }

void cGZDrawContext::BeginBatch()
{
    ++mBatchDepth;
    mEngine.p->vf17();
    mEngine.p->vf18();
}

void cGZDrawContext::EndBatch()
{
    if (mBatchDepth != 0 && --mBatchDepth == 0) mEngine.p->vf19();
}

void cGZDrawContext::DrawPrimitives(uint32_t kind, const float *points, int32_t count)
{
    mPoints.resize(count * 2);
    for (int32_t i = 0; i < count; ++i) {
        mPoints[2 * i] = points[2 * i] + mOriginX;
        mPoints[2 * i + 1] = points[2 * i + 1] + mOriginY;
    }
    FlushState();
    Dispatch(kind, mPoints.data(), count);
}

void cGZDrawContext::DrawPrimitivesAbsolute(uint32_t kind, const float *points, int32_t count)
{
    FlushState();
    Dispatch(kind, points, count);
}

bool cGZDrawContext::LockSurface(cGZDrawSurface *surface, uint32_t readOnly)
{
    return mEngine.p->LockSurface(surface, readOnly);
}

void cGZDrawContext::UnlockSurface() { mEngine.p->UnlockSurface(); }

void cGZDrawContext::FlushState()
{
    if (!mEngine.p) return;
    if (mStateDirty) {
        mStateDirty = false;
        if (mSent.word != mState.word) {
            mSent.word = mState.word;
            mEngine.p->vf8(mState.word);
        }
        if (mSent.byte2 != mState.byte2) {
            mSent.byte2 = mState.byte2;
            mEngine.p->vf9(mState.byte2);
        }
        if (mSent.byte1 != mState.byte1) {
            mSent.byte1 = mState.byte1;
            mEngine.p->vf10(mState.byte1);
        }
        if (mSent.byte0 != mState.byte0) {
            mSent.byte0 = mState.byte0;
            mEngine.p->vf11(mState.byte0);
        }
        const float width = mState.lineWidth;
        if (mSent.lineWidth != width) { // NaN differs from everything, -0 equals 0
            mSent.lineWidth = width;
            mSent.lineWidthRounded = static_cast<int32_t>(width + 0.5f);
            if (mSent.lineWidthRounded == 0) mSent.lineWidthRounded = 1;
            mEngine.p->SetLineWidth(width);
        }
    }
    if (mClipDirty) {
        mClipDirty = false;
        cGZRect clip;
        if (mTextured && !mTextureClipped)
            Intersect(&clip, &mClip, &mTextureArea);
        else
            clip = mClip;
        if (clip.left != mSentClip.left || clip.top != mSentClip.top || clip.right != mSentClip.right ||
            clip.bottom != mSentClip.bottom) {
            mSentClip = clip;
            mEngine.p->SetClip(&clip);
        }
    }
}

void cGZDrawContext::Dispatch(uint32_t kind, const float *points, int32_t count)
{
    switch (kind) {
    case 0:
        BuildPoints(points, count);
        return;
    case 1:
        BuildLines(points, count);
        return;
    case 2:
        switch (mEngine.p->ChooseRoute(2, points, count)) {
        case 1: BuildCurveQuads(points, count); return;
        case 2: BuildCurveSpans(points, count); return;
        }
        return;
    case 3:
        switch (mEngine.p->ChooseRoute(3, points, count)) {
        case 1: {
            // Each rectangle (two corners) as the quad's four corners, clockwise from the first.
            int32_t rects = count >> 1;
            mQuads.resize(rects * 8);
            float *quad = mQuads.data();
            for (const float *r = points; rects != 0; --rects, r += 4, quad += 8) {
                quad[0] = r[0];
                quad[1] = r[1];
                quad[2] = r[2];
                quad[3] = r[1];
                quad[4] = r[2];
                quad[5] = r[3];
                quad[6] = r[0];
                quad[7] = r[3];
            }
            mQuadsOut.swap(mQuads);
            DrawQuadList(mQuadsOut.data(),
                                                  static_cast<int32_t>(static_cast<uint32_t>(mQuadsOut.size()) >> 1));
            return;
        }
        case 2: BuildRectSpans(points, count); return;
        }
        return;
    case 4:
        switch (mEngine.p->ChooseRoute(4, points, count)) {
        case 1: BuildEllipseOutlineQuads(points, count); return;
        case 2: BuildEllipseOutlineCurves(points, count); return;
        }
        return;
    case 5:
        switch (mEngine.p->ChooseRoute(5, points, count)) {
        case 1: BuildEllipsePolygons(points, count); return;
        case 2: BuildEllipseSpans(points, count); return;
        }
        return;
    case 6:
        DrawKind6(points, count);
        return;
    case 7:
        DrawQuadList(points, count);
        return;
    case 8:
        BuildPolygon(points, count);
        return;
    }
}

void cGZDrawContext::DrawKind6(const float *points, int32_t count)
{
    if (mEngine.p->ChooseRoute(6, points, count) == 2) BuildTriangleSpans(points, count);
}

cGZDrawContext *__fastcall cGZDrawContextAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZDrawContext;
}

void __fastcall cGZDrawContextAdapters::Dispatch(cGZDrawContext *self, void *, uint32_t kind, const float *points,
                                                 int32_t count)
{
    self->Dispatch(kind, points, count);
}
