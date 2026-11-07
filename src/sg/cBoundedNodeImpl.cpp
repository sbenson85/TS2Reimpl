// nGZSceneGraph::cBoundedNodeImpl (lane OB). See the header.
#include "sg/cBoundedNodeImpl.h"

#include "game/game.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "misc/SmallVirtuals.h" // nGZEvent::cNullEvent::kVTable
#include "gz/cEventFaces.h"    // nGZEvent::NullEventVTable (the owned word)
#include "sg/SGWorldTransformRefresh.h"

#include <cstring>
#include <new>

namespace nGZSceneGraph {

cBoundedNodeImpl *cBoundedNodeImpl::Construct() { return ::new (this) cBoundedNodeImpl; }

void cBoundedNodeImpl::RefreshIfStale(cICompositionTreeNode *node)
{
    if ((mFlags >> kStaleFlag & 1) != 0) RefreshWorldTransforms(node);
}

void cBoundedNodeImpl::CachedFrame(AffineTransform3 *frame)
{
    frame->mUnknown0 = 1;
    std::memcpy(frame->mOffset, mWorld.Translation(), sizeof frame->mOffset);
    game_code::SetFrameRotation(frame, mWorld.Rotation());
}

cBoundBox *cBoundedNodeImpl::GetBoundBox(cBoundBox *out, cICompositionTreeNode *node)
{
    RefreshIfStale(node);
    const float *t = mWorld.Translation();
    std::memcpy(out->mMin, t, sizeof out->mMin);
    std::memcpy(out->mMax, t, sizeof out->mMax);
    return out;
}

cBoundBox *cBoundedNodeImpl::GetBoundBoxIn(cBoundBox *out, cICompositionTreeNode *node, const cTransform34 *frame)
{
    RefreshIfStale(node);
    AffineTransform3 mine;
    CachedFrame(&mine);
    const AffineTransform3 *result = game_code::ComposeFrames(&mine, reinterpret_cast<const AffineTransform3 *>(frame));
    std::memcpy(out->mMin, result->mOffset, sizeof out->mMin);
    std::memcpy(out->mMax, result->mOffset, sizeof out->mMax);
    return out;
}

void cBoundedNodeImpl::Read(cIBoundedNode *outer, ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cTransformNodeImpl::Read(outer, reader);
    const bool bound = reader->GetBool();
    if ((mFlags >> kReadSkipsBoundFlag & 1) == 0) outer->SetBoundFlag(bound);
    if (info.mName) info.mName->Release();
}

void cBoundedNodeImpl::SetBoundFlag(cIBoundedNode *outer, bool value)
{
    if (((mFlags >> kBoundFlag) & 1) == static_cast<uint32_t>(value)) return;
    SetFlag(kBoundFlag, value);
    NodeBoundFlagChanged(outer);
    const void *nullEvent = nGZEvent::NullEventVTable();
    outer->NotifyListeners(kBoundChangedEvent, 0, &nullEvent);
}

bool cBoundedNodeImpl::IsKindOf(uint32_t iid) { return iid == kIid || cTransformNodeImpl::IsKindOf(iid); }

cPersistTypeInfo *cBoundedNodeImpl::GetTypeInfo(cPersistTypeInfo *out)
{
    out->mName = NewTypeName("cBoundedNode");
    out->mWord4 = 0;
    out->mVersion = kVersion;
    return out;
}

void cBoundedNodeImpl::Write(cIBoundedNode *outer, ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info;
    writer->WriteTypeInfo(GetTypeInfo(&info));
    if (info.mName) info.mName->Release();
    cTransformNodeImpl::Write(outer, writer);
    writer->SetUint8((mFlags >> kBoundFlag) & 1);
}

} // namespace nGZSceneGraph
