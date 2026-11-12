// nGZSceneGraph::cViewerRefNodeRecursiveImpl (lane VF, scenegraph N3r). See the header.
#include "sg/cViewerRefNodeRecursiveImpl.h"

#include "compgeom/CompGeom.h"
#include "compgeom/ConvexMath.h"
#include "compgeom/Frames.h"
#include "compgeom/Matrix4.h"
#include "compgeom/SeparatingAxes.h" // FramePlacement
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZPropertySet.h" // cIGZPropertySetString, GZServiceString
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "misc/UtilitiesE.h" // GetCachedQueriedObject: the material manager
#include "rz/RZAutoRef.h"
#include "rz/RZFrameworkHelpers.h"
#include "sg/DetailObject.h"
#include "sg/SGUtilities.h" // ProjectBox
#include "sg/SGWorldTransformRefresh.h"
#include "sg/cIDisplayList.h"
#include "sg/cIViewerRefNode.h"
#include "sg/cMaterialManager.h"
#include "sg/cTransformNodeImpl.h" // SetFrameRotation

#include <bit>
#include <cmath>
#include <cstring>

namespace nGZSceneGraph {

namespace {

cIMaterialManager *MaterialManager() { return static_cast<cIMaterialManager *>(GetCachedQueriedObject()); }

cIDisplayList *DetailList() { return static_cast<cIDisplayList *>(GetDetailObject()); }

// Unsigned to float through double, as the original converts it (cvtdq2pd with the unsigned fix-up).
float UnsignedToFloat(uint32_t value) { return static_cast<float>(static_cast<double>(value)); }

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::SubS;

} // namespace

cViewerRefNodeRecursiveImpl::cViewerRefNodeRecursiveImpl()
{
    mWordCC = 0;
    mVec[0] = 1.0f;
    mVec[1] = mVec[2] = mVec[3] = 0.0f;
    mScaleE0 = 1.0f;
    mWordE4 = 0;
}

cViewerRefNodeRecursiveImpl::~cViewerRefNodeRecursiveImpl()
{
    if (mDetailCount != 0) DetailList()->FreeSlots(mDetailSlot, mDetailCount);
}

float *cViewerRefNodeRecursiveImpl::FillViewport(float *out)
{
    cViewerTargetSize size;
    GetTargetSize(&size);
    const float halfWidth = UnsignedToFloat(size.mWidth) * 0.5f;
    const float halfHeight = UnsignedToFloat(size.mHeight) * 0.5f;
    for (int i = 0; i < 16; ++i) out[i] = 0.0f;
    out[0] = halfWidth;
    out[3] = halfWidth;
    out[5] = -halfHeight;
    out[7] = halfHeight;
    out[10] = 1.0f;
    out[15] = 1.0f;
    return out;
}

void cViewerRefNodeRecursiveImpl::SetMaterialCount(uint32_t count)
{
    cIDisplayList *const list = DetailList();
    mDetailSlot = list->ReallocSlots(mDetailSlot, mDetailCount, count);
    mDetailCount = count;
    mMaterials.resize(count);
}

void cViewerRefNodeRecursiveImpl::SetMaterial(uint32_t index, const char *name)
{
    if (index >= mMaterials.size()) mMaterials.resize(index + 1);
    // The named material found or loaded and counted (0xae8840), or none (0xae8820 with -1: counted too).
    const ShapeMaterialHandle material(name != nullptr ? static_cast<int32_t>(MaterialManager()->FindOrLoad(name)) : -1);
    mMaterials.data()[index].Assign(material);
}

// ---- the mirrored view's geometry ----

namespace recursive_view {

void PlaneToFrame(const AffineTransform3 *frame, float *out, const float *in)
{
    float m[9];
    nGZCompGeom::AsFrame(*frame).InverseMatrix(m);
    out[0] = AddS(AddS(MulS(m[0], in[0]), MulS(m[3], in[1])), MulS(m[6], in[2]));
    out[1] = AddS(AddS(MulS(m[4], in[1]), MulS(m[1], in[0])), MulS(m[7], in[2]));
    out[2] = AddS(AddS(MulS(m[5], in[1]), MulS(m[2], in[0])), MulS(m[8], in[2]));
    const float *const offset = frame->mOffset;
    out[3] = SubS(in[3], AddS(AddS(MulS(out[1], offset[1]), MulS(out[0], offset[0])), MulS(out[2], offset[2])));
}

float *Subtract3(float *self, const float *other)
{
    for (int i = 0; i < 9; ++i) self[i] = SubS(self[i], other[i]);
    return self;
}

float *__cdecl Transpose4(float *out, const float *m)
{
    // Bit copies (no float load), element by element in the original's order.
    auto *const to = reinterpret_cast<uint32_t *>(out);
    const auto *const from = reinterpret_cast<const uint32_t *>(m);
    for (int k = 0; k < 16; ++k) to[(k & 3) * 4 + (k >> 2)] = from[k];
    return out;
}

float *MultiplyBy4(float *self, const float *b)
{
    float product[16];
    nGZCompGeom::Matrix4Multiply(product, self, b);
    std::memcpy(self, product, sizeof product);
    return self;
}

AffineTransform3 *__cdecl Reflect(AffineTransform3 *out, const AffineTransform3 *frame, const float *plane)
{
    float carried[4];
    std::memcpy(carried, plane, sizeof carried);
    float n[4];
    PlaneToFrame(frame, n, carried);

    const float xx = MulS(n[0], n[0]);
    const float yx = MulS(n[1], n[0]);
    const float zx = MulS(n[2], n[0]);
    const float zy = MulS(n[2], n[1]);
    const float yy = MulS(n[1], n[1]);
    const float zz = MulS(n[2], n[2]);
    const float twice[9] = {AddS(xx, xx), AddS(yx, yx), AddS(zx, zx), AddS(yx, yx), AddS(yy, yy),
                            AddS(zy, zy), AddS(zx, zx), AddS(zy, zy), AddS(zz, zz)};
    float r[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    Subtract3(r, twice); // I - 2 n n^T

    const float k = MulS(n[3], -2.0f);
    nGZCompGeom::FramePlacement mirror;
    mirror.mUnknown0 = 0x1f;
    mirror.mMatrix[0] = -r[0];
    mirror.mMatrix[1] = -r[1];
    mirror.mMatrix[2] = -r[2];
    for (int i = 3; i < 9; ++i) mirror.mMatrix[i] = r[i];
    mirror.mOffset[0] = -MulS(n[0], k);
    mirror.mOffset[1] = MulS(n[1], k);
    mirror.mOffset[2] = MulS(n[2], k);

    nGZCompGeom::FramePlacement result;
    std::memcpy(&result, frame, sizeof result);
    result.Compose(&mirror);
    std::memcpy(out, &result, sizeof result);
    out->mUnknown0 = 3;
    return out;
}

void __cdecl ObliqueClip(float *projection, const AffineTransform3 *frame, const float *plane)
{
    float carried[4];
    std::memcpy(carried, plane, sizeof carried);
    float p[4];
    PlaneToFrame(frame, p, carried);
    float inverse[16];
    nGZCompGeom::Matrix4Inverse(inverse, projection);
    float t[16];
    Transpose4(t, inverse);

    float v[4];
    for (int j = 0; j < 4; ++j) {
        const float *const row = t + 4 * j;
        v[j] = AddS(AddS(AddS(MulS(p[1], row[1]), MulS(p[0], row[0])), MulS(p[2], row[2])), MulS(p[3], row[3]));
    }
    // A plane through the eye (or a NaN's comparison failing the other way: carried on).
    if (std::bit_cast<float>(0x2f03126fu) >= std::fabs(v[3])) return;
    if (v[3] > 0.0f)
        for (float &x : v) x = -x;

    const float clip[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                            v[0], v[1], v[2], v[3], 0.0f, 0.0f, 0.0f, 1.0f};
    MultiplyBy4(projection, clip);
}

} // namespace recursive_view

void cViewerRefNodeRecursiveImpl::Cull(cIBoundedNode *outer, cViewerView *view, uint32_t)
{
    using namespace recursive_view;
    using nGZCompGeom::FramePlacement;

    if (mPlaneFlags & 1) {
        RZAutoRef settings{nullptr};
        auto *const framework = reinterpret_cast<cIGZFrameWork *>(static_cast<uintptr_t>(RZGetGlobal10C9378()));
        if (framework != nullptr)
            framework->GetSystemService(cViewerRefNodeImpl::kSettingsService, cViewerRefNodeImpl::kSettingsIid,
                                        &settings.mObject);
        gz::AnyBoolean value(1);
        GZServiceString key("reflectionWithExtraViewer");
        // ORIGINAL-BUG: the settings are asked unchecked (no configuration service crashes here).
        static_cast<cIGZPropertySetString *>(settings.mObject)->GetPropertyValue(&key, &value);
        settings.ReleaseObject();
        if (!value.mValue) return;
    }

    // The view's drawer is re-read for each call, as the original does. Once a frame.
    auto *const list = reinterpret_cast<cIDisplayList *const *>(&view->mDrawer);
    const uint32_t frame = (*list)->GetFrame();
    if (mWord78 == frame) return;
    mWord78 = frame;

    // The view's rectangle (+0x84), depth range (+0x94) and projection (+0x144, past what cViewerView names).
    const auto *const raw = reinterpret_cast<const uint8_t *>(view);
    float rect[4];
    std::memcpy(rect, raw + 0x84, sizeof rect);
    float projection[16];
    std::memcpy(projection, raw + 0x144, sizeof projection);
    float viewport[16];
    FillViewport(viewport);
    RefreshIfStale(outer);
    FramePlacement eye;
    CachedFrame(&eye);

    if (mBound != nullptr && !mBound->IsEverywhere()) {
        FramePlacement mirrored;
        if (mPlaneFlags & 1) {
            const cRigidTransform world = mBound->GetWorldTransform();
            float plane[4];
            std::memcpy(plane, mPlane, sizeof plane);
            reinterpret_cast<const nGZCompGeom::RigidTransform3 &>(world).TransformPlane(plane); // carried by it (0x19ca50)
            Reflect(&mirrored, reinterpret_cast<const AffineTransform3 *>(&view->mFrame), plane);
            ObliqueClip(projection, &mirrored, plane);
            FramePlacement back = mirrored;
            eye = *back.Invert();
        } else {
            FramePlacement back = eye;
            mirrored = *back.Invert();
        }

        if (mPlaneFlags & 2) {
            // The bound's box on screen: its frame then the view's, through the projection.
            const cRigidTransform world = mBound->GetWorldTransform();
            FramePlacement placed;
            placed.mUnknown0 = 1;
            std::memcpy(placed.mOffset, world.Translation(), sizeof placed.mOffset);
            game_code::SetFrameRotation(&placed, world.Rotation());
            placed.Compose(&mirrored);
            const FramePlacement toView = placed;
            float placement[16];
            nGZCompGeom::AsFrame(toView).ToMatrix4(placement);
            float mvp[16];
            nGZCompGeom::Matrix4Multiply(mvp, placement, projection);

            nGZCompGeom::RigidTransform3 inverse;
            std::memcpy(&inverse, &world, sizeof inverse);
            inverse.Invert();
            FramePlacement local;
            local.mUnknown0 = 1;
            std::memcpy(local.mOffset, &inverse.translation, sizeof local.mOffset);
            game_code::SetFrameRotation(&local, inverse.rotation);
            const cBoundBox box = mBound->GetBoundBoxIn(reinterpret_cast<const cTransform34 *>(&local));
            float extent[6]; // the screen box: low x, y, z then high x, y, z
            nGZSceneGraph::ProjectBox(extent, extent + 3, &box, mvp); // 0xaf22d0

            const float minX = extent[0] > -1.0f ? extent[0] : -1.0f;
            const float minY = extent[1] > -1.0f ? extent[1] : -1.0f;
            const float maxX = extent[3] < 1.0f ? extent[3] : 1.0f;
            const float maxY = extent[4] < 1.0f ? extent[4] : 1.0f;

            // The projection narrowed to that box.
            const float dx = maxX - minX;
            const float dy = maxY - minY;
            float narrow[16] = {};
            narrow[0] = 2.0f / dx;
            narrow[3] = (-maxX - minX) / dx;
            narrow[5] = 2.0f / dy;
            narrow[7] = (-maxY - minY) / dy;
            narrow[10] = 1.0f;
            narrow[15] = 1.0f;
            MultiplyBy4(projection, narrow);

            // The viewport mapped onto it.
            float scale[16] = {};
            scale[0] = MulS(maxX - minX, 0.5f);
            scale[3] = MulS(AddS(maxX, minX), 0.5f);
            scale[5] = MulS(maxY - minY, 0.5f);
            scale[7] = MulS(AddS(maxY, minY), 0.5f);
            scale[10] = 1.0f;
            scale[15] = 1.0f;
            float mapped[16];
            nGZCompGeom::Matrix4Multiply(mapped, scale, viewport);
            std::memcpy(viewport, mapped, sizeof viewport);

            // The rectangle as fractions of the target (y down).
            rect[0] = AddS(MulS(minX, 0.5f), 0.5f);
            rect[1] = 0.5f - MulS(maxY, 0.5f);
            rect[2] = AddS(MulS(maxX, 0.5f), 0.5f);
            rect[3] = 0.5f - MulS(minY, 0.5f);
        }
    }

    // At least a pixel of the target each way (a NaN draws nothing).
    cViewerTargetSize size;
    GetTargetSize(&size);
    if (!((rect[2] - rect[0]) * UnsignedToFloat(size.mWidth) >= 1.0f)) return;
    if (!((rect[3] - rect[1]) * UnsignedToFloat(size.mHeight) >= 1.0f)) return;

    Pass *const pass = (*list)->DrawViewer(static_cast<cIViewerRefNodeBase *>(outer),
                                           &rect[0], &rect[2], reinterpret_cast<const float *>(raw + 0x94), &eye,
                                           projection, viewport);
    const uint32_t parentWord = mParent != nullptr ? mParent->GetFromParent32() : 0;
    // ORIGINAL-BUG: the parent's word is called through unchecked (a node with no parent crashes here).
    RawSlot<93, void>(reinterpret_cast<void *>(static_cast<uintptr_t>(parentWord)), pass);
    (*list)->DrawPass(static_cast<uint32_t>(mMaterials.size()),
                      mMaterials.empty() ? nullptr : reinterpret_cast<const int32_t *>(mMaterials.data()),
                      static_cast<uint32_t>(mDetailSlot));
}

bool cViewerRefNodeRecursiveImpl::IsKindOf(uint32_t iid) { return iid == kIid || cViewerRefNodeBaseImpl::IsKindOf(iid); }

cPersistTypeInfo *cViewerRefNodeRecursiveImpl::GetTypeInfo(cPersistTypeInfo *out)
{
    out->mName = NewTypeName("cViewerRefNodeRecursive");
    out->mWord4 = kTypeWord;
    out->mVersion = kVersion;
    return out;
}

void cViewerRefNodeRecursiveImpl::Read(cIBoundedNode *outer, ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cViewerRefNodeBaseImpl::Read(outer, reader);
    ReadViewMaterials(mMaterials, reader);
    if (info.mName) info.mName->Release();
}

void cViewerRefNodeRecursiveImpl::Write(cIBoundedNode *outer, ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info;
    writer->WriteTypeInfo(GetTypeInfo(&info));
    if (info.mName) info.mName->Release();
    cViewerRefNodeBaseImpl::Write(outer, writer);
    WriteViewMaterials(mMaterials, writer);
}

void cViewerRefNodeRecursiveImpl::CloneInto(cICompositionTreeNode *source, cICompositionTreeNode *clone,
                                            cViewerRefNodeRecursiveImpl *cloneImpl, uint32_t mode,
                                            cINodeCloneContext *context)
{
    cViewerRefNodeBaseImpl::CloneInto(source, clone, cloneImpl, mode, context);
    if (&cloneImpl->mMaterials != &mMaterials) cloneImpl->mMaterials = mMaterials;
}

} // namespace nGZSceneGraph
