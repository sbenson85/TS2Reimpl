// nGZSceneGraph::cViewerRefNodeBaseImpl (lane PY). See the header.
#include "sg/cViewerRefNodeBaseImpl.h"

#include "compgeom/CompGeom.h"
#include "game/game.h"
#include "game/owned.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/DeviceTargetSize.h"
#include "gz/GraphicServices.h"
#include "gz/cConnectionManager.h"
#include "gz/cDeviceDX.h"
#include "gz/cEventSystemService.h"
#include "gz/cIGZFrameWork.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters.h"
#include "sg/SGLocalString.h"
#include "sg/SGTreeCode.h"
#include "sg/cIViewerRefNode.h"
#include "sg/cTextureManager.h"

#include <cstring>
#include <new>

TS2_OWNED_STATIC(float, 10f8d1c)[4]; // what the constructor copies to mVec (zero until something writes it)

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kEventA884a15 = 0xa884a15;         // the bound node's event the viewer listens to (tentative)
constexpr uint32_t kBehaviorIid = 0xfb859324;         // cIAbstractBehaviorType, not counted
constexpr uint32_t kEventServiceId = 0x5b1e896f;      // the event system service
constexpr uint32_t kEventServiceIid = 0x1b1e895a;
constexpr uint32_t kFrameworkEvent = 0xbcf4e33;       // what the constructor asks the framework for (slot 3)
constexpr uint32_t kTextureQueryKind = 5;
constexpr uint32_t kTextureIid = 0xb96cc22;          // what the handle's object is asked for: the texture target
constexpr uint32_t kConnectionChangedEvent = 0x5b51e806;
constexpr uint32_t kFlagBoundChanged = 0xd;          // the held bound's flags the event handler forwards
constexpr uint32_t kFlagBound = 0xb;

// A held reference as the original assigns it: the new one counted before it is stored, the old one released after.
inline void AssignRef(cIGZUnknown *&slot, cIGZUnknown *value)
{
    cIGZUnknown *const old = slot;
    if (value == old) return;
    if (value) value->AddRef();
    slot = value;
    if (old) old->Release();
}

// A reference dropped: emptied before it is released.
inline void ReleaseNull(cIGZUnknown *&slot)
{
    if (cIGZUnknown *const held = slot) {
        slot = nullptr;
        held->Release();
    }
}

// The texture manager (0xae8750) and its counted texture handles (4 bytes each), ours (sg/cTextureManager.h): made
// from an id (0xae8260) or a name (0xae8280), assigned (0xae82e0), released (0xae82b0) or reset (0xae8710).
__forceinline void HandleFromId(uint32_t *handle, uint32_t id) { cTextureHandle::At(handle)->InitFromId(id); }
__forceinline void HandleFromName(uint32_t *handle, const char *name) { cTextureHandle::At(handle)->InitFromName(name); }
__forceinline void HandleAssign(uint32_t *handle, const uint32_t *source)
{
    cTextureHandle::At(handle)->Assign(cTextureHandle::At(source));
}
__forceinline void HandleRelease(uint32_t *handle) { cTextureHandle::At(handle)->Destroy(); }
__forceinline void HandleReset(uint32_t *handle) { cTextureHandle::At(handle)->Reset(); }
__forceinline cIGZUnknown *HandleGet(uint32_t *handle) { return cTextureHandle::At(handle)->Get(); }

} // namespace

// ---- the constructor and destructor ----

cViewerRefNodeBaseImpl *cViewerRefNodeBaseImpl::Construct() { return ::new (this) cViewerRefNodeBaseImpl; }

// (0xae06d0) The renderable level's first (the base class), then the viewer's words.
cViewerRefNodeBaseImpl::cViewerRefNodeBaseImpl()
{
    mRef70 = nullptr;
    mEvent74 = nullptr;
    mWord78 = 0xffffffffu;
    mMode = 1;
    mScale = 1.0f;
    for (cIGZUnknown *&texture : mTextures) texture = nullptr;
    mRef94 = nullptr;
    mTextureHandle = 0xffffffffu;
    mColorSurface = nullptr;
    mDepthSurface = nullptr;
    mRefA4 = nullptr;
    mRefA8 = nullptr;
    mBound = nullptr;
    for (float &v : mPlane) v = 0.0f;
    mPlaneFlags = 0;
    mWordC4 = 0;
    mLayerTagMatch = 2;
    mVec[0] = 1.0f;
    mVec[1] = mVec[2] = mVec[3] = 0.0f;
    mWordCC = 3;
    std::memcpy(mVec, ts2_rva_10f8d1c, sizeof(mVec)); // (the game's 16 bytes: zero until something writes them)
    mScaleE0 = 1.0f;
    mWordE4 = 0;
    void *const framework = GetDirectorFrameworkObject();
    ReleaseNull(mEvent74);
    RawSlot<3>(framework, kFrameworkEvent, 1u, &mEvent74);
    AddLayerBits(0xffffffffu);
    mLayerTag = 0;
    ReleaseNull(mRef70);
    SetRefA8(nullptr);
}

cViewerRefNodeBaseImpl::~cViewerRefNodeBaseImpl()
{
    ReleaseNull(mRef70);
    ReleaseNull(mRefA4);
    ReleaseNull(mRefA8);
    // (the members' own releases, in reverse order of construction)
    if (mRefA8) mRefA8->Release();
    if (mRefA4) mRefA4->Release();
    if (mDepthSurface) mDepthSurface->Release();
    if (mColorSurface) mColorSurface->Release();
    HandleRelease(&mTextureHandle);
    if (mRef94) mRef94->Release();
    for (int i = 3; i >= 0; --i)
        if (mTextures[i]) mTextures[i]->Release();
    if (mEvent74) mEvent74->Release();
    if (mRef70) mRef70->Release();
}

void cViewerRefNodeBaseImpl::Destruct() { this->~cViewerRefNodeBaseImpl(); }

// ---- the targets ----

cIGZUnknown *cViewerRefNodeBaseImpl::GetDepthSurface()
{
    if (mColorSurface) return mDepthSurface;
    nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    if (device && device->IsDeviceReady()) return device->GetTargetProviderSurface();
    return nullptr;
}

cIGZUnknown *cViewerRefNodeBaseImpl::GetColorSurface()
{
    if (mColorSurface) return mColorSurface;
    nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    if (device && device->IsDeviceReady()) return device->GetBackBuffer(0);
    return nullptr;
}

bool cViewerRefNodeBaseImpl::UsesDeviceTargets() { return mColorSurface == nullptr; }

// ---- the held bound node ----

bool cViewerRefNodeBaseImpl::BoundSlot70() { return mBound ? mBound->IsEverywhere() : false; }
bool cViewerRefNodeBaseImpl::BoundSlot73(uint32_t a, uint32_t b) { return mBound ? mBound->IntersectsBound(a, b) : false; }
bool cViewerRefNodeBaseImpl::IntersectsLinear(nGZCompGeom::cILinear3 *linear)
{
    return mBound ? mBound->IntersectsLinear(linear) : false;
}
bool cViewerRefNodeBaseImpl::FirstHitLinear(float *t, nGZCompGeom::cILinear3 *linear)
{
    return mBound ? mBound->FirstHitLinear(t, linear) : false;
}

cBoundBox *cViewerRefNodeBaseImpl::GetBoundBox(cBoundBox *out, cICompositionTreeNode *node)
{
    cBoundBox box;
    if (mBound)
        box = mBound->GetBoundBox();
    else
        box = *cBoundedNodeImpl::GetBoundBox(&box, node);
    *out = box;
    return out;
}

cBoundBox *cViewerRefNodeBaseImpl::GetBoundBoxIn(cBoundBox *out, cICompositionTreeNode *node, const cTransform34 *frame)
{
    cBoundBox box;
    if (mBound)
        box = mBound->GetBoundBoxIn(frame);
    else
        box = *cBoundedNodeImpl::GetBoundBoxIn(&box, node, frame);
    *out = box;
    return out;
}

void cViewerRefNodeBaseImpl::SetBound(cIBoundedNode *outer, cIBoundedNode *bound)
{
    if (mBound) {
        mBound->Disconnect(kEventA884a15, outer, kEventA884a15);
        if (mBound) mBound->Disconnect(kBoundChangedEvent, outer, kBoundChangedEvent);
    }
    mBound = bound;
    mPlaneFlags = 0;
    if (!bound) return;
    bound->Connect(kEventA884a15, outer, kEventA884a15);
    void *service = nullptr;
    if (auto *const framework = static_cast<cIGZFrameWork *>(GetServiceObject()))
        framework->GetSystemService(kEventServiceId, kEventServiceIid, &service);
    // (the original calls through the answer without a test: the event system is always there)
    bool connected = true;
    if (service) {
        nGZEvent::cIConnectionManager *const manager =
            static_cast<nGZEvent::cIEventSystemService *>(service)->GetConnectionManager();
        void *self = nullptr;
        void *held = nullptr;
        outer->QueryInterface(kBehaviorIid, &self);
        mBound->QueryInterface(kBehaviorIid, &held);
        connected = manager->IsConnected(static_cast<cIGZUnknown *>(held), kBoundChangedEvent, static_cast<cIGZUnknown *>(self),
                                         kBoundChangedEvent);
    }
    ReleaseHeldObject(service);
    if (!connected) mBound->Connect(kBoundChangedEvent, outer, kBoundChangedEvent);
}

void cViewerRefNodeBaseImpl::SetBoundAndPlane(cIBoundedNode *outer, cIBoundedNode *bound, const float *plane, uint32_t flags)
{
    SetBound(outer, bound);
    mPlaneFlags = flags;
    std::memcpy(mPlane, plane, sizeof(mPlane));
}

// ---- the texture ----

uint32_t cViewerRefNodeBaseImpl::CallTextureSlot7()
{
    if (mMode != 0) return mMode;
    cIGZUnknown *const texture = HandleGet(&mTextureHandle);
    return RawSlot<7>(texture);
}

bool cViewerRefNodeBaseImpl::IsDeviceViewMode() { return mMode == 1 && mTextures[0] == nullptr; }

void cViewerRefNodeBaseImpl::SetSurfaces(cIGZUnknown *color, cIGZUnknown *depth)
{
    AssignRef(mColorSurface, color);
    AssignRef(mDepthSurface, depth);
}

void cViewerRefNodeBaseImpl::SetRefA8(cIGZUnknown *ref) { AssignRef(mRefA8, ref); }
void cViewerRefNodeBaseImpl::SetRefA4(cIGZUnknown *ref) { AssignRef(mRefA4, ref); }

void cViewerRefNodeBaseImpl::SetExplicit(cIGZUnknown *texture, cIGZUnknown *ref94, float scale)
{
    mMode = 1;
    mScale = scale;
    AssignRef(mTextures[0], texture);
    AssignRef(mRef94, ref94);
    for (int i = 1; i < 4; ++i) ReleaseNull(mTextures[i]);
    HandleReset(&mTextureHandle);
}

void cViewerRefNodeBaseImpl::SetTextures(uint32_t count, cIGZUnknown *const *textures, cIGZUnknown *ref94, float scale)
{
    mMode = count;
    mScale = scale;
    cIGZUnknown **const held = mTextures;
    for (uint32_t i = 0; i < count; ++i) AssignRef(held[i], textures[i]); // (the original does not clamp the count)
    AssignRef(mRef94, ref94);
    for (uint32_t i = count; i < 4; ++i) ReleaseNull(held[i]);
    HandleReset(&mTextureHandle);
}

void cViewerRefNodeBaseImpl::SetTexture(uint32_t id)
{
    uint32_t made;
    HandleFromId(&made, id);
    HandleAssign(&mTextureHandle, &made);
    HandleRelease(&made);
    if (mTextureHandle == 0xffffffffu) {
        SetExplicit(nullptr, nullptr, 1.0f);
        return;
    }
    mMode = 0;
    for (cIGZUnknown *&texture : mTextures) ReleaseNull(texture);
}

void cViewerRefNodeBaseImpl::SetTextureByName(const char *name, uint32_t group)
{
    if (!name) {
        SetExplicit(nullptr, nullptr, 1.0f);
        return;
    }
    uint32_t found = 0xffffffffu;
    uint32_t made;
    if (group) {
        const uint32_t id = VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, const char *, uint32_t), 20>(
            TextureManager(), group, name, kTextureQueryKind);
        HandleFromId(&made, id);
    } else {
        HandleFromName(&made, name);
    }
    HandleAssign(&found, &made);
    HandleRelease(&made);
    SetTexture(found);
    HandleRelease(&found);
}

cIGZUnknown *cViewerRefNodeBaseImpl::GetHandleTexture()
{
    if (mTextureHandle == 0xffffffffu) return nullptr;
    cIGZUnknown *const object = HandleGet(&mTextureHandle);
    cIGZUnknown *held = nullptr;
    if (!object->QueryInterface(kTextureIid, reinterpret_cast<void **>(&held))) {
        if (held) held->Release();
        return nullptr;
    }
    cIGZUnknown *result = nullptr;
    if (RawSlot<6, bool>(held)) {
        if (held) held->AddRef(); // (the original copies the pointer into a counted temporary, then drops it)
        result = held;
        if (held) held->Release();
    }
    if (held) held->Release();
    return result;
}

cViewerTargetSize *cViewerRefNodeBaseImpl::GetTargetSize(cViewerTargetSize *out)
{
    out->mWidth = 1;
    out->mHeight = 1;
    out->mScale = 1.0f;
    if (mMode == 1) {
        if (!mTextures[0]) {
            if (nGZGraphic4::GetCurrentDeviceCached()) {
                nGZGraphic4::cDeviceDisplaySize size;
                const nGZGraphic4::cDeviceDisplaySize *const answer = nGZGraphic4::GetDeviceTargetSize(&size);
                std::memcpy(out, answer, sizeof(*out));
            }
            return out;
        }
    } else if (mMode == 0) {
        cIGZUnknown *const texture = GetHandleTexture();
        if (texture) {
            out->mWidth = RawSlot<20>(RawSlot<17, cIGZUnknown *>(texture, 0u));
            out->mHeight = RawSlot<21>(RawSlot<17, cIGZUnknown *>(texture, 0u));
            out->mScale = RawSlot<16, float>(texture);
        }
        return out;
    }
    // ORIGINAL-BUG: mode >= 2 with no first texture dereferences null here, as in the original.
    out->mWidth = RawSlot<20>(mTextures[0]);
    out->mHeight = RawSlot<21>(mTextures[0]);
    out->mScale = mScale;
    return out;
}

cIGZUnknown *cViewerRefNodeBaseImpl::GetTextureRef94()
{
    if (mMode == 1) {
        if (mTextures[0]) return mRef94;
        nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
        if (device && device->IsDeviceReady()) return device->GetTargetProviderSurface();
        return nullptr;
    }
    if (mMode != 0) return mRef94;
    // ORIGINAL-BUG: an invalid handle (-1) gives a null object, which is dereferenced here as in the original.
    cIGZUnknown *const object = HandleGet(&mTextureHandle);
    cIGZUnknown *held = nullptr;
    if (object->QueryInterface(kTextureIid, reinterpret_cast<void **>(&held)) && RawSlot<6, bool>(object)) {
        cIGZUnknown *const result = RawSlot<18, cIGZUnknown *>(held);
        if (held) held->Release();
        return result;
    }
    if (held) held->Release();
    return nullptr;
}

cIGZUnknown *cViewerRefNodeBaseImpl::GetTextureN(int32_t index)
{
    if (mMode == 1) {
        if (mTextures[0]) return mTextures[index];
        nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
        if (device && device->IsDeviceReady()) return device->GetBackBuffer(0);
        return nullptr;
    }
    if (mMode != 0) return mTextures[index];
    // ORIGINAL-BUG: an invalid handle (-1) gives a null object, which is dereferenced here as in the original.
    cIGZUnknown *const object = HandleGet(&mTextureHandle);
    cIGZUnknown *held = nullptr;
    if (object->QueryInterface(kTextureIid, reinterpret_cast<void **>(&held)) && RawSlot<6, bool>(object)) {
        cIGZUnknown *const result = RawSlot<17, cIGZUnknown *>(held, index);
        if (held) held->Release();
        return result;
    }
    if (held) held->Release();
    return nullptr;
}

// ---- the cull walk and the events ----

void cViewerRefNodeBaseImpl::CullAndDraw(cIBoundedNode *outer, cViewerView *view, uint32_t mask)
{
    if (view->mNode == outer) return;
    if (!mBound) {
        RawSlot<27, void>(view->mDrawer, outer, 0u);
        return;
    }
    if (mPlaneFlags & kPlaneActiveFlag) {
        const cRigidTransform world = mBound->GetWorldTransform();
        float plane[4];
        std::memcpy(plane, mPlane, sizeof(plane));
        reinterpret_cast<const nGZCompGeom::RigidTransform3 &>(world).TransformPlane(plane); // carried by the transform (0x19ca50)
        const float side = plane[1] * view->mEye[1];
        const float behind = ((side + plane[0] * view->mEye[0]) + plane[2] * view->mEye[2]) + plane[3];
        if (0.0f > behind) return;
    }
    if (uint8_t(mask) == 0x3f) {
        RawSlot<27, void>(view->mDrawer, outer, 0x3fu);
        return;
    }
    const cBoundBox box = mBound->GetBoundBoxIn(&view->mFrame);
    const float halfX = (box.mMax[0] - box.mMin[0]) * 0.5f;
    const float halfY = (box.mMax[1] - box.mMin[1]) * 0.5f;
    const float halfZ = (box.mMax[2] - box.mMin[2]) * 0.5f;
    const nGZCompGeom::Vec3 half{halfX, halfY, halfZ};
    const nGZCompGeom::Vec3 centre{box.mMin[0] + halfX, halfY + box.mMin[1], halfZ + box.mMin[2]};
    uint8_t planes = uint8_t(mask);
    if (view->mFrustum.ClassifyBoxAt(&planes, &half, &centre) <= 0)
        RawSlot<27, void>(view->mDrawer, outer, (mask & 0xffffff00u) | planes);
}

void cViewerRefNodeBaseImpl::OnEvent(cIBoundedNode *outer, uint32_t type, uint32_t arg1, void *data, uint32_t sender)
{
    cCompositionTreeNodeImpl::OnEvent(outer, type, arg1, data, sender);
    const auto boundIsSender = [&] { return mBound && static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mBound)) == sender; };
    if (type != kEventA884a15) {
        if (type != kConnectionChangedEvent) {
            if (type != kBoundChangedEvent) return;
            if (boundIsSender()) SetBoundFlag(outer, mBound->TestFlag(kFlagBound));
            return;
        }
        const auto *const body = static_cast<const nGZEvent::cConnectionChangedEvent *>(data);
        if (body->mDisconnected == 1 && reinterpret_cast<uintptr_t>(body->mTarget) == reinterpret_cast<uintptr_t>(outer) &&
            (body->mTargetEvent == kEventA884a15 || body->mTargetEvent == kBoundChangedEvent))
            mBound = nullptr;
    }
    if (boundIsSender() && mBound->TestFlag(kFlagBoundChanged)) NodeBoundFlagChanged(outer);
    if (boundIsSender()) SetBoundFlag(outer, mBound->TestFlag(kFlagBound));
}

// ---- persistence ----

void cViewerRefNodeBaseImpl::Read(cIBoundedNode *outer, ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cRenderableNodeImpl::Read(outer, reader);
    if (info.mVersion >= 2) {
        if (reader->GetBool()) {
            nTSSG::SGLocalString text;
            reader->GetGZStr(&text.value);
            SetTextureByName(text.Text(), 0);
        }
        if (info.mVersion >= 3 && reader->GetBool()) {
            cIGZUnknown *bound = nullptr;
            reader->GetObjectRef(cIBoundedNode::kIid, &bound);
            float plane[4];
            reader->GetFloat32Array(plane, 4);
            uint32_t flags = reader->GetUint32();
            if (info.mVersion < 5 && (flags & 1)) flags |= kPlaneActiveFlag;
            SetBoundAndPlane(outer, static_cast<cIBoundedNode *>(bound), plane, flags);
            if (bound) bound->Release();
        }
    }
    mLayerTagMatch = reader->GetUint8();
    mWordC4 = reader->GetUint32();
    if (info.mVersion >= 4) {
        mWordCC = reader->GetUint32();
        mVec[0] = reader->GetFloat32();
        mVec[1] = reader->GetFloat32();
        mVec[2] = reader->GetFloat32();
        mVec[3] = reader->GetFloat32();
        mScaleE0 = reader->GetFloat32();
        mWordE4 = reader->GetUint32();
    }
    if (info.mName) info.mName->Release();
}

// ---- the clone (lane TZ) ----

void cViewerRefNodeBaseImpl::CloneInto(cICompositionTreeNode *source, cICompositionTreeNode *clone,
                                       cViewerRefNodeBaseImpl *cloneImpl, uint32_t mode, cINodeCloneContext *context)
{
    cRenderableNodeImpl::CloneInto(source, clone, cloneImpl, mode, context);
    cloneImpl->mMode = mMode;
    for (int i = 0; i < 4; ++i) AssignRef(cloneImpl->mTextures[i], mTextures[i]);
    AssignRef(cloneImpl->mRef94, mRef94);
    HandleAssign(&cloneImpl->mTextureHandle, &mTextureHandle);
    AssignRef(cloneImpl->mColorSurface, mColorSurface);
    AssignRef(cloneImpl->mDepthSurface, mDepthSurface);
    auto *const target = static_cast<cIViewerRefNodeBase *>(clone);
    if (context) {
        // The context's clone of the bound (asked also for no bound), counted for us.
        cIBoundedNode *bound = nullptr;
        context->FindClone(mBound, cIBoundedNode::kIid, &bound);
        target->Slot101(bound ? bound : mBound, mPlane, mPlaneFlags);
        if (bound) bound->Release();
    } else {
        target->Slot101(mBound, mPlane, mPlaneFlags);
    }
    target->SetLayerTagMatch(mLayerTagMatch);
    target->Slot97(mWordC4);
    target->Slot103(mRef70);
}

bool cViewerRefNodeBaseImpl::IsKindOf(uint32_t iid) { return iid == kIid || cRenderableNodeImpl::IsKindOf(iid); }

cPersistTypeInfo *cViewerRefNodeBaseImpl::GetTypeInfo(cPersistTypeInfo *out)
{
    out->mName = NewTypeName("cViewerRefNodeBase");
    out->mWord4 = 0;
    out->mVersion = kVersion;
    return out;
}

void cViewerRefNodeBaseImpl::Write(cIBoundedNode *outer, ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info;
    writer->WriteTypeInfo(GetTypeInfo(&info));
    if (info.mName) info.mName->Release();
    cRenderableNodeImpl::Write(outer, writer);
    if (mTextureHandle != 0xffffffffu) {
        uint32_t record[4];
        char *const *const answer = VirtualCall<char *const *(__thiscall *)(void *, void *, uint32_t), 24>(
            TextureManager(), record, mTextureHandle);
        char *const name = *answer;
        writer->SetUint8(name != nullptr);
        if (name) writer->WriteRZCharStr(name);
    } else {
        writer->SetUint8(0);
    }
    const bool hasBound = mBound != nullptr;
    writer->SetUint8(hasBound);
    if (mBound) {
        cIGZUnknown *ref = nullptr;
        mBound->QueryInterface(1, reinterpret_cast<void **>(&ref));
        writer->WriteObjectRef(ref);
        if (ref) ref->Release();
        writer->SetFloat32Array(mPlane, 4);
        writer->SetUint32(mPlaneFlags);
    }
    writer->SetByte(mLayerTagMatch);
    writer->SetUint32(mWordC4);
    writer->SetUint32(mWordCC);
    writer->SetFloat32(mVec[0]);
    writer->SetFloat32(mVec[1]);
    writer->SetFloat32(mVec[2]);
    writer->SetFloat32(mVec[3]);
    writer->SetFloat32(mScaleE0);
    writer->SetUint32(mWordE4);
}

} // namespace nGZSceneGraph
