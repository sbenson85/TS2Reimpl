// nGZSceneGraph::cRenderableNodeImpl (lane PX). See the header.
#include "sg/DetailObject.h"
#include "sg/cRenderableNodeImpl.h"

#include "game/original.h"
#include "game/vtable.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "sg/SGLocalString.h"

#include <cstring>
#include <new>

namespace nGZSceneGraph {

namespace {

// The detail object (0xaead80, __cdecl): slot 43 is the limit a renderable node caches.
cIRenderGroupRegistry *Registry() { return RenderGroupRegistry(); }

uint32_t DetailLimit()
{
    void *detail = nGZSceneGraph::GetDetailObject();
    return VirtualCall<uint32_t(__thiscall *)(void *), 43>(detail);
}

} // namespace

cRenderableNodeImpl *cRenderableNodeImpl::Construct() { return ::new (this) cRenderableNodeImpl; }

void cRenderableNodeImpl::RefreshDetailLimit() { mDetailLimit = DetailLimit(); }

void cRenderableNodeImpl::AddLayer(const char *name) { AddLayerBits(1u << (Registry()->BitOfName(name) & 31)); }

void cRenderableNodeImpl::AddLayerBits(uint32_t bits)
{
    const uint32_t layers = mLayers | bits;
    if (mLayers == layers) return;
    mLayers = layers;
    RefreshDetailLimit();
}

void cRenderableNodeImpl::RemoveLayer(const char *name) { RemoveLayerBits(1u << (Registry()->BitOfName(name) & 31)); }

void cRenderableNodeImpl::RemoveLayerBits(uint32_t bits)
{
    const uint32_t layers = ~bits & mLayers;
    if (mLayers == layers) return;
    mLayers = layers;
    RefreshDetailLimit();
}

uint32_t cRenderableNodeImpl::CountLayers()
{
    if (mLayers == kAllLayers) return Registry()->Count();
    uint32_t n = 0;
    for (uint32_t m = mLayers; m; m &= m - 1) ++n;
    return n;
}

cRenderGroupNameRange *cRenderableNodeImpl::MakeLayerNames(cRenderGroupNameRange *out)
{
    auto *const end = new cRenderGroupNameIterator(mLayers, cRenderGroupNameIterator::kEnd);
    auto *const begin = new cRenderGroupNameIterator(mLayers, 0);
    out->mCurrent = begin->Clone();
    if (out->mCurrent) out->mCurrent->AddRef();
    out->mBegin = begin;
    begin->AddRef();
    out->mEnd = end;
    end->AddRef();
    return out;
}

void cRenderableNodeImpl::SetWorldTransform(const cRigidTransform *transform, uint32_t, uint32_t)
{
    SetFlag(kStaleFlag, false);
    mWorld = *transform;
    RefreshDetailLimit();
}

void cRenderableNodeImpl::Read(cIBoundedNode *outer, ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cBoundedNodeImpl::Read(outer, reader);
    mLayers = 0;
    const bool all = reader->GetBool();
    const uint32_t count = reader->GetUint32();
    for (uint32_t i = 0; i < count; ++i) {
        nTSSG::SGLocalString name;
        reader->GetGZStr(&name.value);
        AddLayer(name.Text());
    }
    if (all) AddLayerBits(kAllLayers);
    mLayerTag = reader->GetUint32();
    SetFlag(kPersistedFlag, reader->GetBool());
    if (info.mName) info.mName->Release();
}

bool cRenderableNodeImpl::IsKindOf(uint32_t iid) { return iid == kIid || cBoundedNodeImpl::IsKindOf(iid); }

cPersistTypeInfo *cRenderableNodeImpl::GetTypeInfo(cPersistTypeInfo *out)
{
    out->mName = NewTypeName("cRenderableNode");
    out->mWord4 = 0;
    out->mVersion = kVersion;
    return out;
}

void cRenderableNodeImpl::Write(cIBoundedNode *outer, ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info;
    writer->WriteTypeInfo(GetTypeInfo(&info));
    if (info.mName) info.mName->Release();
    cBoundedNodeImpl::Write(outer, writer);
    writer->SetUint8(mLayers == kAllLayers);
    writer->SetUint32(CountLayers());
    cRenderGroupNameRange range;
    MakeLayerNames(&range);
    while (!range.mCurrent->IsEqual(range.mEnd)) {
        writer->WriteRZCharStr(const_cast<char *>(range.mCurrent->Get()));
        range.mCurrent->Next();
    }
    if (range.mEnd) range.mEnd->Release();
    if (range.mBegin) range.mBegin->Release();
    if (range.mCurrent) range.mCurrent->Release();
    writer->SetUint32(mLayerTag);
    writer->SetUint8((mFlags >> kPersistedFlag) & 1);
}

void cRenderableNodeImpl::CloneInto(cICompositionTreeNode *source, cICompositionTreeNode *clone,
                                    cRenderableNodeImpl *cloneImpl, uint32_t mode, cINodeCloneContext *context)
{
    cTransformNodeImpl::CloneInto(source, clone, cloneImpl, mode, context);
    auto *const node = static_cast<cIRenderableNode *>(clone);
    node->RemoveLayerBits(kAllLayers);
    node->AddLayerBits(mLayers);
    node->SetLayerTag(mLayerTag);
}

} // namespace nGZSceneGraph
