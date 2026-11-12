// nTSSG::cOverlayNode (lane 261108d, scenegraph row LT1). See the header.
#include "sg/cOverlayNode.h"

#include "compgeom/CompGeom.h"
#include "compgeom/Frames.h"         // FrameMath
#include "compgeom/Frustum.h"
#include "compgeom/SeparatingAxes.h" // FramePlacement
#include "misc/UtilitiesE.h"         // GetCachedQueriedObject: the material manager
#include "sg/DetailObject.h"
#include "sg/cIDisplayList.h"
#include "sg/cMaterialManager.h"
#include "sg/cNodeCloneContext.h"
#include "sg/cTransformNode.h"       // cTransformNode, node_family
#include "sg/cTransformNodeImpl.h"
#include "sg/cViewerRefNodeBaseImpl.h" // cViewerView

#include <cfloat>
#include <cstring>
#include <new>

namespace nTSSG {

using namespace nGZSceneGraph;

namespace {

cIMaterialManager *MaterialManager() { return static_cast<cIMaterialManager *>(GetCachedQueriedObject()); }

// The named material found or loaded (0xae8840), then the handle takes it (0xae88a0).
void AssignNamed(ShapeMaterialHandle &handle, const char *name)
{
    const ShapeMaterialHandle found(static_cast<int32_t>(MaterialManager()->FindOrLoad(name)));
    handle.Assign(found);
}

cIDisplayList *SlotList() { return static_cast<cIDisplayList *>(GetDetailObject()); }

// The geometry's bound: six floats at +0x14 of its info block.
const nGZCompGeom::BoundBox *GeometryBound(cIOverlayGeometry *geometry)
{
    return reinterpret_cast<const nGZCompGeom::BoundBox *>(reinterpret_cast<const uint8_t *>(geometry->GetFlags()) + 0x14);
}

// A counted setter: the new one counted first, then the old one released.
template <class T> void Exchange(cOverlayNode::Held<T> &held, T *object)
{
    T *const old = held.mObject;
    if (object == old) return;
    if (object) object->AddRef();
    held.mObject = object;
    if (old) old->Release();
}

// Clears first, then releases.
template <class T> void Drop(cOverlayNode::Held<T> &held)
{
    if (T *const object = held.mObject) {
        held.mObject = nullptr;
        object->Release();
    }
}

constexpr const char *kMaterialNames[cOverlayNode::kMaterialCount] = {
    "OverlayDecalMaterial",          "OverlayAdditiveMaterial",      "OverlayModulateMaterial",
    "OverlayDecalInverseDepthMaterial", "OverlayDrawNoOverlapMaterial", "OverlayShadowMaterial",
    "OverlaySoftShadowMaterial",     "OverlayDebugMaterial",
};

} // namespace

// The copy is a plain cTransformNode, as the particles node's (the same shared body, 0xa3a3b0).
void cOverlayNode::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context)
{
    cICompositionTreeNode *clone = nullptr;
    if (context) {
        context->FindClone(this, cITransformNode::kIid, &clone);
    } else {
        clone = new cTransformNode;
        clone->AddRef();
    }
    if (mode != 2) {
        auto *const cloneImpl = reinterpret_cast<cTransformNodeImpl *>(reinterpret_cast<uint8_t *>(clone) + 0xc);
        static_cast<cTransformNodeImpl &>(mImpl).CloneInto(this, clone, cloneImpl, mode, context);
    }
    clone->QueryInterface(iid, out);
    if (clone) clone->Release();
}

void *cOverlayNode::MakeIterator43(void *out, uint32_t a, uint32_t b) { return node_family::BuildIterator43(&mImpl, out, this, a, b); }
void *cOverlayNode::MakeIterator44(void *out, uint32_t a, uint32_t b) { return node_family::BuildIterator44(&mImpl, out, this, a, b); }
void *cOverlayNode::MakeIterator51(void *out, uint32_t a) { return node_family::BuildIterator51(&mImpl, out, a); }

void cOverlayNode::OnContentChanged(uint32_t transform, uint32_t b, uint32_t c)
{
    mImpl.SetWorldTransform(reinterpret_cast<const cRigidTransform *>(uintptr_t(transform)), b, c);
}

cBoundBox cOverlayNode::GetBoundBoxIn(const cTransform34 *frame)
{
    cBoundBox box = GetBoundBox();
    reinterpret_cast<nGZCompGeom::BoundBox &>(box).Transform(reinterpret_cast<const AffineTransform3 *>(frame));
    return box;
}

cBoundBox cOverlayNode::GetBoundBox()
{
    nGZCompGeom::BoundBox box;
    if (mGeometry.mObject) {
        std::memcpy(&box, GeometryBound(mGeometry.mObject), sizeof box);
    } else {
        box.min.x = box.min.y = box.min.z = FLT_MAX;
        box.max.x = box.max.y = box.max.z = -FLT_MAX;
    }
    return reinterpret_cast<const cBoundBox &>(box);
}

void cOverlayNode::Draw(void *viewer, uint32_t planeMask)
{
    auto *const view = static_cast<cViewerView *>(viewer);
    auto *const list = static_cast<cIDisplayList *>(view->mDrawer);
    if (list->GetFrame() != mFrame) {
        mFrame = list->GetFrame();
        mMaterialsBuilt = false;
    }
    if (Update()) {
        const int32_t count = static_cast<int32_t>(mEntries.size());
        if (mSlots.mCount < static_cast<uint32_t>(count)) {
            mSlots.mStart = SlotList()->ReallocSlots(mSlots.mStart, mSlots.mCount, count);
            mSlots.mCount = count;
        }
        mMaterials.resize(count);
        // The entry's material index is not checked against the eight.
        const bool perEntry = static_cast<int32_t>(mMode) <= 1;
        for (int32_t i = 0; i < count; ++i) {
            const OverlayEntry &entry = mEntries[i];
            if (!entry.mEnabled) mMaterials[i] = -1;
            else mMaterials[i] = mHandles[perEntry ? entry.mMaterial : kDebugMaterial].mIndex;
        }
        mMaterialsBuilt = true;
    }
    if (mEntries.empty() || !mGeometry.mObject) return;

    // The node's world frame, then the view's.
    const cRigidTransform world = GetWorldTransform();
    nGZCompGeom::FrameMath frame;
    frame.mUnknown0 = 1;
    std::memcpy(frame.mOffset, world.Translation(), sizeof frame.mOffset);
    frame.SetRotation(world.Rotation());
    const AffineTransform3 placed = *reinterpret_cast<nGZCompGeom::FramePlacement &>(frame).Compose(
        reinterpret_cast<const nGZCompGeom::FramePlacement *>(&view->mFrame));
    // The test leaves its verdict in the mask's low byte; a mask of all six planes skips it.
    if ((planeMask & 0xff) != 0x3f &&
        view->mFrustum.ClassifyBox(reinterpret_cast<uint8_t *>(&planeMask), GeometryBound(mGeometry.mObject), &placed) > 0)
        return;
    list->DrawNode(this, &placed, GeometryBound(mGeometry.mObject), planeMask);
}

void cOverlayNode::Slot83(void *viewer, void *)
{
    if (!mGenerator.mObject || !mGeometry.mObject) return;
    auto *const list = static_cast<cIDisplayList *>(static_cast<cViewerView *>(viewer)->mDrawer);
    const uint32_t *subsetWords = nullptr;
    uint32_t word24 = 0;
    list->AddGeometrySubsets(reinterpret_cast<cIGeometryRender *>(mGeometry.mObject), 0x81, mMaterials.data(), &subsetWords,
                             &word24, nullptr, nullptr, mSlots.mStart);
}

bool cOverlayNode::Init()
{
    if (mInitialised) return false;
    mInitialised = true;
    for (uint32_t i = 0; i < kMaterialCount; ++i) AssignNamed(mHandles[i], kMaterialNames[i]);
    return true;
}

bool cOverlayNode::Shutdown()
{
    if (!mInitialised) return false;
    mInitialised = false;
    mEntries.clear();
    mList.clear();
    Drop(mGenerator);
    if (mGeometry.mObject) {
        mGeometry.mObject->Detach();
        Drop(mGeometry);
    }
    return true;
}

void cOverlayNode::SetGenerator(cIOverlayGenerator *generator) { Exchange(mGenerator, generator); }

void cOverlayNode::SetGeometry(cIOverlayGeometry *geometry)
{
    Exchange(mGeometry, geometry);
    geometry->Attach(static_cast<cIOverlayNode *>(this));
}

bool cOverlayNode::Update()
{
    if (!mGenerator.mObject || !mGenerator.mObject->Generate(&mEntries)) return false;
    if (mGeometry.mObject) mGeometry.mObject->Build(&mEntries);
    return true;
}

void cOverlayNode::SetMaterial(uint32_t index, const char *name)
{
    if (index <= 6) AssignNamed(mHandles[index], name);
}

const OverlayEntry *cOverlayNode::GetEntry(int32_t index, uint8_t *built)
{
    if (built) *built = mMaterialsBuilt;
    if (index >= 0 && index < static_cast<int32_t>(mEntries.size())) return &mEntries[index];
    return &mDefault;
}

cOverlayNode *__fastcall cOverlayNodeAdapters::Construct(Self *memory, void *) { return ::new (memory) cOverlayNode; }

void __fastcall cOverlayNodeAdapters::Destruct(Self *self, void *) { self->cOverlayNode::~cOverlayNode(); }

} // namespace nTSSG
