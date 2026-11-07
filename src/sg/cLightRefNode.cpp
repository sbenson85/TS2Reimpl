// nGZSceneGraph::cLightRefNode (lane TE: scenegraph N2r part 2). See the header.
#include "sg/cLightRefNode.h"

#include "compgeom/CompGeom.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "misc/SmallVirtuals.h"      // nGZEvent::cNullEvent::kVTable
#include "gz/cEventFaces.h"    // nGZEvent::NullEventVTable (the owned word)
#include "sg/SGBehaviorCopies.h"     // the cell and the registry the game's copies share
#include "sg/SGTreeCode.h"           // NodeTransformChanged
#include "sg/cLightRefAttributeNameIterator.h"
#include "sg/cObjectGraphNodeImpl.h" // the subordinate notifications, NewTypeName
#include "sg/cTransformNode.h"       // node_family
#include "sg/cTransformNodeImpl.h"   // game_code::ComposeFrames
#include "sg/cViewerRefNodeBaseImpl.h" // cViewerView

#include <cfloat>
#include <new>

namespace nGZSceneGraph {

static_assert(sizeof(cLightRefNode) == cLightRefNode::kSize, "cLightRefNode size");

namespace {

constexpr uint32_t kLightIid = 0x9c5ed164; // cILight

nGZCompGeom::cIBound *ShapeOf(cILight *light) { return static_cast<nGZCompGeom::cIBound *>(light->GetBoundShape()); }

// The shape the boxes use: the copy's when there is a copy, else the light's, else none.
nGZCompGeom::cIBound *BoundShapeOf(const cLightRefNodeImpl &impl)
{
    if (impl.mHeld74) return ShapeOf(impl.mHeld74);
    if (impl.mLight) return ShapeOf(impl.mLight);
    return nullptr;
}

const nGZCompGeom::RigidTransform3 &AsRigid(const cRigidTransform &rigid)
{
    return *reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(&rigid);
}

// The box of a light that has no shape: everything.
cBoundBox *Everything(cBoundBox *out)
{
    for (int i = 0; i < 3; ++i) {
        out->mMin[i] = -FLT_MAX;
        out->mMax[i] = FLT_MAX;
    }
    return out;
}

// A point as a box (min and max the point).
cBoundBox *PointBox(cBoundBox *out, const float *point)
{
    for (int i = 0; i < 3; ++i) out->mMin[i] = out->mMax[i] = point[i];
    return out;
}

void Inflate(nGZCompGeom::BoundBox *box, float by)
{
    box->min.x -= by;
    box->min.y -= by;
    box->min.z -= by;
    box->max.x += by;
    box->max.y += by;
    box->max.z += by;
}

void SendLightChanged(cICompositionTreeNode *outer)
{
    const void *nullEvent = nGZEvent::NullEventVTable();
    outer->NotifyListeners(cLightRefNodeImpl::kLightChangedEvent, 0, &nullEvent);
}

} // namespace

// ---- cLightRefNodeImpl (lane TE) ----

void cLightRefNodeImpl::DropCopy()
{
    cILight *const held = mHeld74;
    if (held) {
        mHeld74 = nullptr;
        held->Release();
    }
}

void cLightRefNodeImpl::CopyFrom(cILight *light)
{
    light->Clone(0, kLightIid, reinterpret_cast<void **>(&mHeld74), nullptr);
}

void cLightRefNodeImpl::OnEvent(cICompositionTreeNode *outer, uint32_t type, uint32_t arg1, void *data, uint32_t sender)
{
    cCompositionTreeNodeImpl::OnEvent(outer, type, arg1, data, sender);
    switch (type) {
    case kSubordinateEvent: {
        const auto *const body = static_cast<const cSubordinateListChanged *>(data);
        if (body->mIid != kLightIid) return;
        if (body->mKind != cSubordinateListChanged::kAdded && body->mKind != cSubordinateListChanged::kRemoved) return;
        NodeTransformChanged(outer);
        if (mLight && mHeld74) {
            DropCopy();
            CopyFrom(mLight);
        }
        break;
    }
    case kDetailChangedEvent:
        RefreshDetailLimit();
        if (mHeld74 && mLight) {
            DropCopy();
            CopyFrom(mLight);
        }
        break;
    case kTransformEvent:
        NodeTransformChanged(outer);
        return;
    default:
        return;
    }
    SendLightChanged(outer);
}

void cLightRefNodeImpl::SetLight(cICompositionTreeNode *outer, cILight *light)
{
    if (light == mLight) return;
    if (mLight) {
        NotifySubordinateRemoving(outer, kLightIid, mLight);
        mLight->RemoveReferrer(outer);
        cILight *const old = mLight;
        mLight = nullptr;
        old->Release();
        NotifySubordinateRemoved(outer, kLightIid);
    }
    if (light) {
        NotifySubordinateAdding(outer, kLightIid);
        cILight *const old = mLight;
        if (light != old) {
            light->AddRef();
            mLight = light;
            if (old) old->Release();
        }
        mLight->AddReferrer(outer);
        NotifySubordinateAdded(outer, kLightIid, light);
    }
}

void cLightRefNodeImpl::CloneInto(cICompositionTreeNode *source, cICompositionTreeNode *clone, cLightRefNodeImpl *cloneImpl,
                                  uint32_t mode, cINodeCloneContext *context)
{
    cRenderableNodeImpl::CloneInto(source, clone, cloneImpl, mode, context);
    auto *const target = static_cast<cILightRefNode *>(clone);
    target->SetLightFlag(mFlag7c);
    target->SetLight(mLight);
    if (mHeld74) {
        cloneImpl->DropCopy();
        mHeld74->Clone(0, kLightIid, reinterpret_cast<void **>(&cloneImpl->mHeld74), nullptr);
    }
}

bool cLightRefNodeImpl::IsKindOf(uint32_t iid) { return iid == kIid || cRenderableNodeImpl::IsKindOf(iid); }

void cLightRefNodeImpl::SetAttribute(cICompositionTreeNode *outer, uint32_t id, uint32_t extra, const cAttributeValue *value)
{
    if (!IsLightAttribute(id)) {
        cTransformNodeImpl::SetAttribute(outer, id, extra, value);
        return;
    }
    if (!mLight || !mLight->GetStandardLight()) return;
    if (!mHeld74) CopyFrom(mLight);
    cIStandardLight *const standard = mHeld74->GetStandardLight();
    const float first = value->mValues[0];
    switch (id) {
    case kConeFalloffAngle: standard->SetConeFalloffAngle(first); break;
    case kRange: standard->SetRange(first); break;
    case kColor: {
        const float rgb[3] = {value->mValues[0], value->mValues[1], value->mValues[2]};
        standard->SetColor(rgb);
        break;
    }
    case kConeAngle: standard->SetConeAngle(first); break;
    case kFalloff: standard->SetFalloff(first); break;
    case kFactor: standard->SetFactor(first); break;
    default: standard->SetIntensity(first); break; // kIntensity
    }
    NodeTransformChanged(outer);
}

void cLightRefNodeImpl::GetAttribute(cAttributeValue *out, uint32_t id, uint32_t extra)
{
    if (!IsLightAttribute(id)) {
        cTransformNodeImpl::GetAttribute(out, id, extra);
        return;
    }
    cILight *const source = BoundLight();
    if (!source) return;
    cIStandardLight *const standard = source->GetStandardLight();
    if (!standard) return;
    switch (id) {
    case kConeFalloffAngle: out->mValues[0] = standard->GetConeFalloffAngle(); break;
    case kRange: out->mValues[0] = standard->GetRange(); break;
    case kColor: {
        float rgb[3];
        const float *const colour = standard->GetColor(rgb);
        out->mValues[0] = colour[0];
        out->mValues[1] = colour[1];
        out->mValues[2] = colour[2];
        break;
    }
    case kConeAngle: out->mValues[0] = standard->GetConeAngle(); break;
    case kFalloff: out->mValues[0] = standard->GetFalloff(); break;
    case kFactor: out->mValues[0] = standard->GetFactor(); break;
    default: out->mValues[0] = standard->GetIntensity(); break; // kIntensity
    }
}

namespace {

// The copy read inline (the reader's slot 24: the object written by the writer's slot 25, as the light interface).
void ReadCopy(cLightRefNodeImpl &impl, ifaces::cIGZPersistReader *reader)
{
    impl.DropCopy();
    RawSlot<24, void>(reader, kLightIid, reinterpret_cast<void **>(&impl.mHeld74));
}

} // namespace

void cLightRefNodeImpl::Read(cICompositionTreeNode *outer, ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cRenderableNodeImpl::Read(static_cast<cIBoundedNode *>(outer), reader);
    auto *const node = static_cast<cILightRefNode *>(outer);
    if (info.mVersion < kVersion) {
        // Before 10: a light count (0 or 1), the flag, then a "has a copy" flag.
        const uint32_t count = reader->GetUint32();
        if (count == 1) {
            cILight *light = nullptr;
            reader->GetObjectRef(kLightIid, &light);
            node->SetLight(light);
            if (light) light->Release();
        }
        mFlag7c = reader->GetUint8();
        const bool hasCopy = reader->GetBool();
        if (hasCopy && count == 1) ReadCopy(*this, reader);
    } else {
        cILight *light = nullptr;
        reader->GetObjectRef(kLightIid, &light);
        node->SetLight(light);
        mFlag7c = reader->GetUint8();
        if (!reader->GetBool()) ReadCopy(*this, reader); // the byte says "no copy"
        if (light) light->Release();
    }
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo *cLightRefNodeImpl::GetTypeInfo(cPersistTypeInfo *out)
{
    out->mName = NewTypeName("cLightRefNode");
    out->mWord4 = kTypeWord;
    out->mVersion = kVersion;
    return out;
}

void cLightRefNodeImpl::Write(cICompositionTreeNode *outer, ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info;
    writer->WriteTypeInfo(GetTypeInfo(&info));
    if (info.mName) info.mName->Release();
    cRenderableNodeImpl::Write(static_cast<cIBoundedNode *>(outer), writer);
    writer->WriteObjectRef(mLight);
    writer->SetByte(mFlag7c);
    writer->SetUint8(mHeld74 == nullptr);
    if (mHeld74) RawSlot<25, void>(writer, mHeld74->GetReferent());
}

// ---- bounds and hits ----

cBoundBox *cLightRefNodeImpl::GetBoundBoxIn(cBoundBox *out, cICompositionTreeNode *outer, const cTransform34 *frame)
{
    nGZCompGeom::cIBound *const part = BoundShapeOf(*this);
    if (mLight && !part) return Everything(out);
    RefreshIfStale(outer);
    AffineTransform3 mine;
    CachedFrame(&mine);
    const AffineTransform3 composed = *game_code::ComposeFrames(&mine, reinterpret_cast<const AffineTransform3 *>(frame));
    if (mLight) {
        part->GetBoundBoxIn(reinterpret_cast<nGZCompGeom::BoundBox *>(out), &composed);
        return out;
    }
    return PointBox(out, composed.mOffset);
}

cBoundBox *cLightRefNodeImpl::GetBoundBox(cBoundBox *out, cICompositionTreeNode *outer)
{
    nGZCompGeom::cIBound *const part = BoundShapeOf(*this);
    if (mLight && !part) return Everything(out);
    RefreshIfStale(outer);
    const nGZCompGeom::RigidTransform3 placed = AsRigid(mWorld);
    if (mLight) {
        nGZCompGeom::BoundBox box;
        part->GetBoundBoxRigid(&box, &placed);
        Inflate(&box, mBoundsInflate);
        *reinterpret_cast<nGZCompGeom::BoundBox *>(out) = box;
        return out;
    }
    const float point[3] = {placed.translation.x, placed.translation.y, placed.translation.z};
    return PointBox(out, point);
}

bool cLightRefNodeImpl::IntersectsBound(cICompositionTreeNode *outer, nGZCompGeom::cIBound *bound, const AffineTransform3 *frame)
{
    if (!mLight) return false;
    nGZCompGeom::cIBound *const part = ShapeOf(BoundLight());
    if (!part) return true;
    RefreshIfStale(outer);
    AffineTransform3 mine;
    CachedFrame(&mine);
    return part->IntersectsBoundInAt(&mine, bound, frame);
}

bool cLightRefNodeImpl::IntersectsLinear(cICompositionTreeNode *outer, nGZCompGeom::cILinear3 *linear)
{
    if (!mLight) return false;
    nGZCompGeom::cIBound *const part = ShapeOf(BoundLight());
    if (!part) return true;
    RefreshIfStale(outer);
    AffineTransform3 mine;
    CachedFrame(&mine);
    return linear->IntersectsBoundIn(part, &mine);
}

bool cLightRefNodeImpl::FirstHitLinear(cICompositionTreeNode *outer, float *t, nGZCompGeom::cILinear3 *linear)
{
    if (!mLight) return false;
    nGZCompGeom::cIBound *const part = ShapeOf(BoundLight());
    if (!part) {
        *t = 0.0f;
        return true;
    }
    RefreshIfStale(outer);
    AffineTransform3 mine;
    CachedFrame(&mine);
    float hit;
    if (linear->FirstHitBoundIn(&hit, part, &mine) && hit < *t) {
        *t = hit;
        return true;
    }
    return false;
}

// ---- the draw ----

void cLightRefNodeImpl::Draw(cICompositionTreeNode *outer, cViewerView *view, uint32_t planeMask)
{
    RawSlot<26, void>(view->mDrawer, outer, planeMask);
}

void cLightRefNodeImpl::Cull(cICompositionTreeNode *outer, cViewerView *view, uint8_t mask)
{
    if (!mLight) return;
    RefreshIfStale(outer);
    AffineTransform3 mine;
    CachedFrame(&mine);
    const AffineTransform3 composed =
        *game_code::ComposeFrames(&mine, reinterpret_cast<const AffineTransform3 *>(&view->mFrame));
    cILight *const light = BoundLight();
    nGZCompGeom::cIBound *const part = ShapeOf(light);
    if (part && mask != 0x3f) {
        nGZCompGeom::BoundBox box;
        part->GetBoundBox(&box);
        Inflate(&box, mBoundsInflate);
        if (view->mFrustum.ClassifyBox(&mask, &box, &composed) > 0) return; // wholly outside
    }
    RawSlot<30, void>(view->mDrawer, light, &composed);
}

// ---- slot 51: the attribute-name range ----

// (0xa61d90) A light attribute's name range: one {group, name} pair, the group the standard light's name ("lightsGroup"
// without one), the name the attribute's ("coneAngle" for the cone angle). The id other than the seven light attributes
// answers the empty range 0xa37d80. The original reads the light's slot 35 three times and slot 49 twice (kept: the
// light's own answers can differ between calls).
void *cLightRefNodeImpl::MakeIterator51(void *outRange, uint32_t id)
{
    auto *const out = static_cast<cArrayAttributeEntryRange *>(outRange);
    const char *group = "lightsGroup";
    if (mLight) {
        if (mLight->GetStandardLight()) {
            if (mLight->GetStandardLight()->GetLightName()) group = mLight->GetStandardLight()->GetLightName();
        }
    }
    const char *name;
    switch (id) {
    case kConeFalloffAngle: name = "conePenumbra"; break;
    case kRange: name = "range"; break;
    case kColor: name = "color"; break;
    case kConeAngle: name = "coneAngle"; break;
    case kFalloff: name = "falloffRate"; break;
    case kFactor: name = "relSpecularScale"; break;
    case kIntensity: name = "diffuseScale"; break;
    default: return MakeEmptyAttributeEntryRange(this, nullptr, out, id);
    }
    return MakeLightAttributeNameRange(out, mLight != nullptr, group, name);
}

// ---- the behaviour's cells ----

nGZEvent::cIConnectionManager *&cLightRefNodeBehaviorTraits::EventManagerCell() { return ts2_rva_10f6380; }

nGZEvent::cEventAttributeMap *cLightRefNodeBehaviorTraits::Registry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return behavior_copies::LightRefRegistryFor(0, &defaults);
}

// ---- cLightRefNode ----

// (The original's operator new throws on failure, so its null branch is dead.)
cLightRefNode *__cdecl cLightRefNode::Create() { return new cLightRefNode; }

cLightRefNode *cLightRefNode::Construct() { return ::new (this) cLightRefNode; }
void cLightRefNode::Destruct() { this->cLightRefNode::~cLightRefNode(); }
cLightRefNode *cLightRefNode::DeletingDestructor(uint32_t flags)
{
    this->cLightRefNode::~cLightRefNode();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

// (0xa60de0) The light let go of through SetLight.
cLightRefNode::~cLightRefNode()
{
    if (mImpl.mLight) mImpl.SetLight(this, nullptr);
}

void cLightRefNode::OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t sender)
{
    Behavior::OnEvent(type, arg1, data, sender);
    mImpl.OnEvent(this, type, arg1, data, sender);
}

nGZEvent::cEventAttribute cLightRefNode::GetEventAttribute(uint32_t event)
{
    const auto found = cLightRefNodeBehaviorTraits::Registry()->find(event);
    if (found == cLightRefNodeBehaviorTraits::Registry()->end()) return nGZEvent::behavior_mt::gMissingRecord;
    nGZEvent::cEventAttribute record = found->second;
    if (cLightRefNodeImpl::IsLightAttribute(event)) record.mWord08 = mImpl.mLight != nullptr;
    return record;
}

bool cLightRefNode::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == cILightRefNode::kIid) {
        *out = static_cast<cILightRefNode *>(this);
        AddRef();
        return true;
    }
    return Base::QueryInterface(iid, out);
}

void cLightRefNode::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context)
{
    cICompositionTreeNode *clone = nullptr;
    if (context) {
        context->FindClone(this, cILightRefNode::kIid, &clone);
    } else {
        clone = new cLightRefNode;
        clone->AddRef();
    }
    if (mode != 2) {
        auto *const cloneImpl = &static_cast<cLightRefNode *>(clone)->mImpl;
        mImpl.CloneInto(this, clone, cloneImpl, mode, context);
    }
    clone->QueryInterface(iid, out);
    if (clone) clone->Release();
}

void *cLightRefNode::MakeIterator43(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator43(&mImpl, out, this, a, b);
}
void *cLightRefNode::MakeIterator44(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator44(&mImpl, out, this, a, b);
}

// 92 (0xa63520): the copy let go of, the transform propagated.
void cLightRefNode::DropLightCopy()
{
    mImpl.DropCopy();
    NodeTransformChanged(this);
}

} // namespace nGZSceneGraph
