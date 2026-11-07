// nGZSceneGraph::cViewerRefNodeImpl (lane TZ, scenegraph N3r). See the header. The view maths (the projection, the
// viewport matrix and the four point transforms) is cViewerRefNodeView.cpp.
#include "sg/cViewerRefNodeImpl.h"

#include "compgeom/CompGeom.h"
#include "compgeom/SeparatingAxes.h" // FramePlacement
#include "game/vtable.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "misc/UtilitiesE.h" // GetCachedQueriedObject: the material manager
#include "sg/DetailObject.h"
#include "sg/SGLocalString.h"
#include "sg/SGWorldTransformRefresh.h"
#include "sg/cIDisplayList.h"
#include "sg/cIViewerRefNode.h"
#include "sg/cIndexedMeshBuilder.h" // QuaternionFromMatrix
#include "sg/cMaterialManager.h"
#include "sg/cTextureManager.h"     // ResolveResourceName
#include "sg/cViewer_LEGACY.h"

#include <cstring>
#include <new>
#include <string>
#include <utility>

namespace nGZSceneGraph {

namespace {

cIMaterialManager *MaterialManager() { return static_cast<cIMaterialManager *>(GetCachedQueriedObject()); }

cIDisplayList *DetailList() { return static_cast<cIDisplayList *>(GetDetailObject()); }

// Unsigned to float through double, as the original converts it (cvtdq2pd with the unsigned fix-up).
float UnsignedToFloat(uint32_t value) { return static_cast<float>(static_cast<double>(value)); }

// The reader's slot 28 (the group of the stream being read; the generated interface stops at slot 26).
uint32_t ReaderGroup(ifaces::cIGZPersistReader *reader)
{
    using Fn = uint32_t(__thiscall *)(void *);
    return (*reinterpret_cast<Fn **>(reader))[28](reader);
}

// Write's view of a material's name: what follows the "!" of a "*...!name" reference (0xda33f8 "*", 0xdbb414 "!").
std::string WrittenMaterialName(const char *name)
{
    std::string text = name;
    if (!text.empty() && text[0] == '*') {
        const size_t bang = text.find('!');
        if (bang != std::string::npos) text = text.substr(bang + 1);
    }
    return text;
}

} // namespace

// ---- the entry ----

cViewerRefEntry::cViewerRefEntry(const cViewerRefEntry &other)
    : mMaterials(other.mMaterials), mDetailSlot(other.mDetailSlot), mAspect(other.mAspect), mDirty(other.mDirty)
{
    std::memcpy(static_cast<void *>(this), &other, offsetof(cViewerRefEntry, mMaterials));
}

cViewerRefEntry::cViewerRefEntry(cViewerRefEntry &&other) noexcept
    : mMaterials(std::move(other.mMaterials)), mDetailSlot(other.mDetailSlot), mAspect(other.mAspect),
      mDirty(other.mDirty)
{
    std::memcpy(static_cast<void *>(this), &other, offsetof(cViewerRefEntry, mMaterials));
}

cViewerRefEntry &cViewerRefEntry::operator=(const cViewerRefEntry &other)
{
    std::memcpy(static_cast<void *>(this), &other, offsetof(cViewerRefEntry, mMaterials));
    if (&mMaterials != &other.mMaterials) mMaterials = other.mMaterials;
    mDetailSlot = other.mDetailSlot;
    mAspect = other.mAspect;
    mDirty = other.mDirty;
    return *this;
}

// ---- construction ----

cViewerRefNodeImpl *cViewerRefNodeImpl::Construct() { return ::new (this) cViewerRefNodeImpl; }

void cViewerRefNodeImpl::Destruct() { this->~cViewerRefNodeImpl(); }

// ---- the entries ----

void cViewerRefNodeImpl::ResizeEntries(uint32_t count)
{
    const uint32_t size = GetEntryCount();
    for (uint32_t i = count; i < size; ++i) {
        cViewerRefEntry &entry = mEntries.data()[i];
        if (entry.mDetailSlot != -1)
            DetailList()->FreeSlots(entry.mDetailSlot, static_cast<uint32_t>(entry.mMaterials.size()));
    }
    mEntries.resize(count);
}

void cViewerRefNodeImpl::SetEntryPlacement(uint32_t index, const cRigidTransform *placement)
{
    mEntries.data()[index].mPlacement = *placement;
}

void cViewerRefNodeImpl::SetMaterialCount(uint32_t index, uint32_t count)
{
    cViewerRefEntry &entry = mEntries.data()[index];
    cIDisplayList *const list = DetailList();
    entry.mDetailSlot = list->ReallocSlots(entry.mDetailSlot, static_cast<uint32_t>(entry.mMaterials.size()), count);
    entry.mMaterials.resize(count);
}

void cViewerRefNodeImpl::SetMaterial(uint32_t index, uint32_t slot, const char *name)
{
    cViewerRefEntry &entry = mEntries.data()[index];
    if (slot >= entry.mMaterials.size()) {
        cIDisplayList *const list = DetailList();
        entry.mDetailSlot =
            list->ReallocSlots(entry.mDetailSlot, static_cast<uint32_t>(entry.mMaterials.size()), slot + 1);
        entry.mMaterials.resize(slot + 1);
    }
    // The named material found or loaded and counted (0xae8840), or none (0xae8820 with -1: counted too).
    const ShapeMaterialHandle material(
        name != nullptr ? static_cast<int32_t>(MaterialManager()->FindOrLoad(name)) : -1);
    entry.mMaterials.data()[slot].Assign(material);
}

void cViewerRefNodeImpl::SetViewport(uint32_t index, float x, float y, float w, float h)
{
    cViewerRefEntry &entry = mEntries.data()[index];
    entry.mViewport[0] = x;
    entry.mViewport[1] = y;
    entry.mViewport[2] = w;
    entry.mViewport[3] = h;
    entry.mDirty = 1;
}

void cViewerRefNodeImpl::SetSubrect(uint32_t index, float x, float y, float w, float h)
{
    cViewerRefEntry &entry = mEntries.data()[index];
    entry.mSubrect[0] = x;
    entry.mSubrect[1] = y;
    entry.mSubrect[2] = w;
    entry.mSubrect[3] = h;
    entry.mDirty = 1;
}

void cViewerRefNodeImpl::SetFixedAspect(uint32_t index, uint8_t fixed)
{
    cViewerRefEntry &entry = mEntries.data()[index];
    entry.mFixedAspect = fixed;
    entry.mDirty = 1;
}

void cViewerRefNodeImpl::SetPixelAspect(uint32_t index, float aspect)
{
    cViewerRefEntry &entry = mEntries.data()[index];
    entry.mPixelAspect = aspect;
    entry.mDirty = 1;
}

void cViewerRefNodeImpl::SetPerspective(uint32_t index, float fov)
{
    cViewerRefEntry &entry = mEntries.data()[index];
    entry.mType = cViewerRefEntry::kPerspective;
    entry.mFov = fov;
    entry.mDirty = 1;
}

void cViewerRefNodeImpl::SetDepthRange(uint32_t index, float min, float max)
{
    cViewerRefEntry &entry = mEntries.data()[index];
    entry.mDepthMin = min;
    entry.mDepthMax = max;
}

void cViewerRefNodeImpl::SetNearFar(uint32_t index, float nearClip, float farClip)
{
    cViewerRefEntry &entry = mEntries.data()[index];
    entry.mNear = nearClip;
    entry.mFar = farClip;
    entry.mDirty = 1;
}

void cViewerRefNodeImpl::SetOffsets(uint32_t index, float x, float y)
{
    cViewerRefEntry &entry = mEntries.data()[index];
    entry.mOffset[0] = x;
    entry.mOffset[1] = y;
    entry.mDirty = 1;
}

void cViewerRefNodeImpl::SetOrtho(uint32_t index, float left, float right, float bottom, float top, float nearClip,
                                  float farClip)
{
    cViewerRefEntry &entry = mEntries.data()[index];
    entry.mOrtho[0] = left;
    entry.mOrtho[1] = right;
    entry.mOrtho[2] = bottom;
    entry.mOrtho[3] = top;
    entry.mNear = nearClip;
    entry.mType = cViewerRefEntry::kOrtho;
    entry.mFar = farClip;
    entry.mDirty = 1;
}

void cViewerRefNodeImpl::SetProjection(uint32_t index, const float *matrix)
{
    cViewerRefEntry &entry = mEntries.data()[index];
    entry.mType = cViewerRefEntry::kExplicit;
    std::memcpy(entry.mProjection, matrix, sizeof entry.mProjection);
}

// ---- the draw ----

void cViewerRefNodeImpl::DrawEntries(cIBoundedNode *outer, cIDisplayList *const *list, uint32_t parentWord)
{
    cViewerTargetSize size;
    GetTargetSize(&size);
    const uint32_t count = GetEntryCount();
    for (uint32_t i = 0; i < count; ++i) {
        cViewerRefEntry *const entry = GetEntry(i);
        const float x0 = entry->mViewport[0] > 0.0f ? entry->mViewport[0] : 0.0f;
        const float y0 = entry->mViewport[1] > 0.0f ? entry->mViewport[1] : 0.0f;
        const float right = entry->mViewport[2] + entry->mViewport[0];
        const float bottom = entry->mViewport[3] + entry->mViewport[1];
        const float x1 = right < 1.0f ? right : 1.0f;
        const float y1 = bottom < 1.0f ? bottom : 1.0f;
        // At least a pixel of the target each way (a NaN skips the entry).
        if (!((x1 - x0) * UnsignedToFloat(size.mWidth) >= 1.0f)) continue;
        if (!((y1 - y0) * UnsignedToFloat(size.mHeight) >= 1.0f)) continue;

        // The eye in the world: the entry's placement, then the node's world transform.
        RefreshIfStale(outer);
        const cRigidTransform world = mWorld;
        cRigidTransform composed = entry->mPlacement;
        const cRigidTransform eye = *game_code::ComposeRigid(&composed, &world);

        float viewport[16];
        GetViewport(viewport, i);

        nGZCompGeom::FramePlacement frame;
        frame.mUnknown0 = 1;
        std::memcpy(frame.mOffset, eye.Translation(), sizeof frame.mOffset);
        game_code::SetFrameRotation(&frame, eye.Rotation());

        const float minimum[2] = {x0, y0};
        const float maximum[2] = {x1, y1};
        const float depth[2] = {entry->mDepthMin, entry->mDepthMax};
        Pass *const pass = (*list)->DrawViewer(static_cast<cIViewerRefNode *>(outer), minimum, maximum, depth,
                                               &frame, entry->mProjection, viewport);
        // ORIGINAL-BUG: the parent's word is called through unchecked (a node with no parent crashes here).
        RawSlot<93, void>(reinterpret_cast<void *>(static_cast<uintptr_t>(parentWord)), pass);
        const auto &materials = entry->mMaterials;
        (*list)->DrawPass(static_cast<uint32_t>(materials.size()),
                          materials.empty() ? nullptr : reinterpret_cast<const int32_t *>(materials.data()),
                          static_cast<uint32_t>(entry->mDetailSlot));
    }
}

void cViewerRefNodeImpl::Render(cIBoundedNode *outer)
{
    RefreshAspect();
    const uint32_t parentWord = mParent != nullptr ? mParent->GetFromParent32() : 0;
    cIDisplayList *const list = DetailList();
    list->BeginScene();
    mWord78 = list->GetFrame();
    DrawEntries(outer, &list, parentWord);
    list->EndScene();
}

void cViewerRefNodeImpl::Cull(cIBoundedNode *outer, cViewerView *view, uint32_t)
{
    // The view's drawer is re-read for each call, as the original does.
    auto *const list = reinterpret_cast<cIDisplayList *const *>(&view->mDrawer);
    const uint32_t frame = (*list)->GetFrame();
    if (mWord78 == frame) return;
    mWord78 = frame;
    const uint32_t parentWord = mParent != nullptr ? mParent->GetFromParent32() : 0;
    DrawEntries(outer, list, parentWord);
}

// ---- the node's other slots ----

void cViewerRefNodeImpl::SetAttribute(cICompositionTreeNode *outer, uint32_t id, uint32_t extra,
                                      const cAttributeValue *value)
{
    if (id == kFovAttribute)
        SetPerspective(extra, value->mValues[0]);
    else if (id == kNearFarAttribute)
        SetNearFar(extra, value->mValues[0], value->mValues[1]);
    else
        cTransformNodeImpl::SetAttribute(outer, id, extra, value);
}

void cViewerRefNodeImpl::GetAttribute(cAttributeValue *out, uint32_t id, uint32_t extra)
{
    if (id == kFovAttribute) {
        out->mValues[0] = GetEntry(extra)->mFov;
    } else if (id == kNearFarAttribute) {
        const cViewerRefEntry *const entry = GetEntry(extra);
        out->mValues[0] = entry->mNear;
        out->mValues[1] = entry->mFar;
    } else {
        cTransformNodeImpl::GetAttribute(out, id, extra);
    }
}

bool cViewerRefNodeImpl::IsKindOf(uint32_t iid) { return iid == kIid || cViewerRefNodeBaseImpl::IsKindOf(iid); }

cPersistTypeInfo *cViewerRefNodeImpl::GetTypeInfo(cPersistTypeInfo *out)
{
    out->mName = NewTypeName("cViewerRefNode");
    out->mWord4 = kTypeWord;
    out->mVersion = kVersion;
    return out;
}

// ---- persistence ----

void ReadViewMaterials(std::vector<cViewerMaterialHandle> &materials, ifaces::cIGZPersistReader *reader)
{
    materials.resize(reader->GetUint32());
    for (uint32_t i = 0; i < materials.size(); ++i) {
        if (!reader->GetBool()) continue;
        nTSSG::SGLocalString name;
        reader->GetGZStr(&name.value);
        const uint32_t group = ReaderGroup(reader);
        if (group == 0) {
            const ShapeMaterialHandle material(static_cast<int32_t>(MaterialManager()->FindOrLoad(name.Text())));
            materials.data()[i].Assign(material);
        } else {
            // The name in the stream's group.
            nTSSG::SGLocalString resolved;
            ResolveResourceName(resolved.value.AsInterface(), name.Text(), group, nullptr);
            const ShapeMaterialHandle material(static_cast<int32_t>(MaterialManager()->FindOrLoad(resolved.Text())));
            materials.data()[i].Assign(material);
        }
    }
}

void cViewerRefNodeImpl::ReadEntry(cViewerRefEntry &entry, uint32_t version, ifaces::cIGZPersistReader *reader)
{
    entry.mType = reader->GetUint32();
    if (version >= 0xe) entry.mFixedAspect = reader->GetBool();
    for (float &f : entry.mViewport) f = reader->GetFloat32();
    for (float &f : entry.mSubrect) f = reader->GetFloat32();
    entry.mNear = reader->GetFloat32();
    entry.mFar = reader->GetFloat32();
    entry.mPixelAspect = reader->GetFloat32();
    entry.mDepthMin = reader->GetFloat32();
    entry.mDepthMax = reader->GetFloat32();
    entry.mFov = reader->GetFloat32();
    for (float &f : entry.mOffset) f = reader->GetFloat32();
    for (float &f : entry.mOrtho) f = reader->GetFloat32();
    // The rotation, then the translation.
    float rotation[4];
    reader->GetFloat32Array(rotation, 4);
    std::memcpy(entry.mPlacement.Rotation(), rotation, sizeof rotation);
    float translation[3];
    for (float &f : translation) f = reader->GetFloat32();
    std::memcpy(entry.mPlacement.Translation(), translation, sizeof translation);
    // A given matrix is kept (and the entry not dirty); version 12 records hold one whatever the type.
    if (version < 0xd || entry.mType == cViewerRefEntry::kExplicit) reader->GetFloat32Array(entry.mProjection, 16);
    entry.mDirty = entry.mType != cViewerRefEntry::kExplicit;

    ReadViewMaterials(entry, reader);
}

void cViewerRefNodeImpl::ReadLegacyEntry(cViewerRefEntry &entry, uint32_t version, ifaces::cIGZPersistReader *reader)
{
    cViewer_LEGACY *legacy = nullptr;
    reader->GetObjectRef(cViewer_LEGACY::kIid, &legacy);
    // ORIGINAL-BUG: the view is copied unchecked (a record naming no legacy viewer crashes here).
    entry = legacy->mView;
    if (version >= 10) {
        float rotation[4];
        if (version < 11) {
            float matrix[9];
            reader->GetFloat32Array(matrix, 9);
            indexedmesh::QuaternionFromMatrix(rotation, matrix);
        } else {
            reader->GetFloat32Array(rotation, 4);
        }
        float translation[3];
        for (float &f : translation) f = reader->GetFloat32();
        entry.mDirty = 1;
        std::memcpy(entry.mPlacement.Translation(), translation, sizeof translation);
        std::memcpy(entry.mPlacement.Rotation(), rotation, sizeof rotation);
    }
    // Released through its virtual base (the vbptr's view of the object), as the original does.
    if (legacy) static_cast<cISGResource *>(legacy)->Release();
}

void cViewerRefNodeImpl::Read(cIBoundedNode *outer, ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cViewerRefNodeBaseImpl::Read(outer, reader);
    const uint32_t count = reader->GetUint32();
    mEntries.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (info.mVersion < 0xc)
            ReadLegacyEntry(mEntries.data()[i], info.mVersion, reader);
        else
            ReadEntry(mEntries.data()[i], info.mVersion, reader);
    }
    if (info.mName) info.mName->Release();
}

void cViewerRefNodeImpl::Write(cIBoundedNode *outer, ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info;
    writer->WriteTypeInfo(GetTypeInfo(&info));
    if (info.mName) info.mName->Release();
    cViewerRefNodeBaseImpl::Write(outer, writer);
    writer->SetUint32(GetEntryCount());
    for (uint32_t i = 0; i < GetEntryCount(); ++i) {
        cViewerRefEntry *const entry = GetEntry(i);
        writer->SetUint32(entry->mType);
        writer->SetUint8(entry->mFixedAspect);
        for (float f : entry->mViewport) writer->SetFloat32(f);
        for (float f : entry->mSubrect) writer->SetFloat32(f);
        writer->SetFloat32(entry->mNear);
        writer->SetFloat32(entry->mFar);
        writer->SetFloat32(entry->mPixelAspect);
        writer->SetFloat32(entry->mDepthMin);
        writer->SetFloat32(entry->mDepthMax);
        writer->SetFloat32(entry->mFov);
        for (float f : entry->mOffset) writer->SetFloat32(f);
        for (float f : entry->mOrtho) writer->SetFloat32(f);
        writer->SetFloat32Array(entry->mPlacement.Rotation(), 4);
        const float *const translation = entry->mPlacement.Translation();
        writer->SetFloat32(translation[0]);
        writer->SetFloat32(translation[1]);
        writer->SetFloat32(translation[2]);
        if (entry->mType == cViewerRefEntry::kExplicit) writer->SetFloat32Array(entry->mProjection, 16);
        WriteViewMaterials(entry->mMaterials, writer);
    }
}

void WriteViewMaterials(const std::vector<cViewerMaterialHandle> &materials, ifaces::cIGZPersistWriter *writer)
{
    writer->SetUint32(static_cast<uint32_t>(materials.size()));
    for (uint32_t m = 0; m < materials.size(); ++m) {
        // The info's first word is the material's name text.
        const MaterialInfo material = MaterialManager()->GetInfo(materials.data()[m].mIndex);
        const char *const name = reinterpret_cast<const char *>(static_cast<uintptr_t>(material.nameHash));
        writer->SetUint8(name != nullptr);
        if (name) {
            std::string text = WrittenMaterialName(name);
            writer->WriteRZCharStr(text.data());
        }
    }
}

void cViewerRefNodeImpl::CloneInto(cICompositionTreeNode *source, cICompositionTreeNode *clone,
                                   cViewerRefNodeImpl *cloneImpl, uint32_t mode, cINodeCloneContext *context)
{
    cViewerRefNodeBaseImpl::CloneInto(source, clone, cloneImpl, mode, context);
    if (&cloneImpl->mEntries != &mEntries) cloneImpl->mEntries = mEntries;
}

} // namespace nGZSceneGraph
