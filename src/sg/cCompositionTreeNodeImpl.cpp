// nGZSceneGraph::cCompositionTreeNodeImpl (lane NZ). See the header.
#include "sg/cCompositionTreeNodeImpl.h"

#include "game/original.h"
#include "sg/SGTreeCode.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"

#include <new>

namespace nGZSceneGraph {

namespace {
constexpr uint32_t kChildren = cICompositionTreeNode::kIid; // the children's list, as notifications name it
} // namespace

cCompositionTreeNodeImpl::~cCompositionTreeNodeImpl()
{
    if (mFirstChild) mFirstChild->Release();
    if (mNextSibling) mNextSibling->Release();
}

cCompositionTreeNodeImpl *cCompositionTreeNodeImpl::Construct() { return ::new (this) cCompositionTreeNodeImpl; }

void cCompositionTreeNodeImpl::Destruct() { this->~cCompositionTreeNodeImpl(); }

void cCompositionTreeNodeImpl::AddChild(cICompositionTreeNode *outer, cICompositionTreeNode *child)
{
    NotifySubordinateAdding(outer, kChildren);
    child->SetLink(cICompositionTreeNode::kParentLink, outer);
    child->SetLink(cICompositionTreeNode::kNextLink, mFirstChild);
    if (mFirstChild) mFirstChild->SetLink(cICompositionTreeNode::kPrevLink, child);
    cICompositionTreeNode *const old = mFirstChild;
    if (child != old) {
        child->AddRef();
        mFirstChild = child;
        if (old) old->Release();
    }
    NotifySubordinateAdded(outer, kChildren, child);
}

void cCompositionTreeNodeImpl::RemoveChild(cICompositionTreeNode *outer, cICompositionTreeNode *child)
{
    NotifySubordinateRemoving(outer, kChildren, child);
    child->AddRef();
    cICompositionTreeNode *const prev = child->GetPrevSibling();
    cICompositionTreeNode *const next = child->GetNextSibling();
    cICompositionTreeNode *const old = mFirstChild;
    if (child == old) {
        if (next != old) {
            if (next) next->AddRef();
            mFirstChild = next;
            if (old) old->Release();
        }
    } else if (prev) {
        prev->SetLink(cICompositionTreeNode::kNextLink, next);
    }
    if (next) next->SetLink(cICompositionTreeNode::kPrevLink, prev);
    child->SetLink(cICompositionTreeNode::kParentLink, nullptr);
    child->SetLink(cICompositionTreeNode::kPrevLink, nullptr);
    child->SetLink(cICompositionTreeNode::kNextLink, nullptr);
    child->Release();
    NotifySubordinateRemoved(outer, kChildren);
}

void cCompositionTreeNodeImpl::SetFlag(uint32_t bit, bool value)
{
    const uint32_t shift = bit & 31;
    mFlags = (static_cast<uint32_t>(value) << shift) | (mFlags & ~(1u << shift));
}

bool cCompositionTreeNodeImpl::IsKindOf(uint32_t iid)
{
    return iid == cICompositionTreeNode::kIid || cObjectGraphNodeImpl::IsKindOf(iid);
}

void cCompositionTreeNodeImpl::OnEvent(cICompositionTreeNode *outer, uint32_t type, uint32_t, void *data, uint32_t)
{
    if (type == cGraphConnChanged::kEventId) OnGraphConnChanged(outer, static_cast<const cGraphConnChanged *>(data));
}

void cCompositionTreeNodeImpl::OnGraphConnChanged(cICompositionTreeNode *, const cGraphConnChanged *body)
{
    if (body->mIid != kChildren) return;
    cICompositionTreeNode *node = nullptr;
    body->mSubordinate->QueryInterface(kChildren, reinterpret_cast<void **>(&node));
    if (body->mDisconnecting == 0)
        NodeConnected(node);
    else if (body->mDisconnecting == 1)
        NodeDisconnecting(node);
    if (node) node->Release();
}

void cCompositionTreeNodeImpl::Read(cICompositionTreeNode *outer, ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cObjectGraphNodeImpl::Read(outer, reader);
    for (uint32_t n = reader->GetUint32(); n != 0; --n) {
        cICompositionTreeNode *child = nullptr;
        reader->GetObjectRef(kChildren, &child);
        outer->AddChild(child); // (unchecked, as in the original: a missing child faults in AddChild)
        if (child) child->Release();
    }
    SetFlag(cICompositionTreeNode::kReadFlag, true);
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo *cCompositionTreeNodeImpl::GetTypeInfo(cPersistTypeInfo *out)
{
    out->mName = NewTypeName("cCompositionTreeNode");
    out->mWord4 = 0;
    out->mVersion = kVersion;
    return out;
}

void cCompositionTreeNodeImpl::Write(cICompositionTreeNode *outer, ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info;
    writer->WriteTypeInfo(GetTypeInfo(&info));
    if (info.mName) info.mName->Release();
    cObjectGraphNodeImpl::Write(outer, writer);
    uint32_t count = 0;
    for (cICompositionTreeNode *c = mFirstChild; c; c = c->GetNextSiblingConst()) ++count;
    writer->SetUint32(count);
    for (cICompositionTreeNode *c = mFirstChild; c; c = c->GetNextSiblingConst()) writer->WriteObjectRef(c);
}

} // namespace nGZSceneGraph
