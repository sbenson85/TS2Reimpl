// nGZSceneGraph::cResourceNode (lane OG). Layout, faces and the switch: see the header.
#include "sg/cResourceNode.h"

#include "game/original.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "sg/SGLocalString.h"
#include "sg/cNodeCloneContext.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName
#include "sg/cTransformNode.h"       // node_family: the iterator builders

#include <new>

namespace nGZSceneGraph {

static_assert(sizeof(cResourceNode) == cResourceNode::kSize, "cResourceNode size");
static_assert(offsetof(cResourceNode, mSource) == 0x54, "cResourceNode source");
static_assert(offsetof(cResourceNode, mWord) == 0x58, "cResourceNode word");

cResourceNode::cResourceNode() { mImpl.SetFlag(3, true); }

cResourceNode::~cResourceNode()
{
    if (cIResourceNode *const source = mSource) {
        mSource = nullptr;
        source->Release();
    }
}

// (The original's operator new throws on failure, so its null branch is dead.)
cResourceNode *__cdecl cResourceNode::Create() { return new cResourceNode; }

uint32_t cResourceNode::GetClassId() { return kClassId; }

bool cResourceNode::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == cIResourceNode::kIid) {
        *out = static_cast<cIResourceNode *>(this);
        AddRef();
        return true;
    }
    if (Node::QueryInterface(iid, out)) return true;
    return cSGResource::QueryInterface(iid, out);
}

uint32_t cResourceNode::AddRef() { return Node::AddRef(); }
uint32_t cResourceNode::Release() { return Node::Release(); }

void cResourceNode::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context)
{
    cIResourceNode *found = nullptr;
    if (context) {
        context->FindClone(this, cIResourceNode::kIid, &found);
    } else {
        found = new cResourceNode;
        found->AddRef();
    }
    if (mode != 2) CopyContentTo(static_cast<cResourceNode *>(found), mode, context);
    found->QueryInterface(iid, out);
    if (found) found->Release();
}

void cResourceNode::CopyContentTo(cResourceNode *clone, uint32_t mode, cINodeCloneContext *context)
{
    CloneCompositionInto(&mImpl, nullptr, this, clone, &clone->mImpl, mode, context);

    // The clone instances what this node instances, or this node itself (a counted temporary holds it meanwhile).
    cIResourceNode *const source = mSource ? mSource : static_cast<cIResourceNode *>(this);
    source->AddRef();
    if (cIResourceNode *const old = clone->mSource; old != source) {
        source->AddRef();
        clone->mSource = source;
        if (old) old->Release();
    }
    source->Release();
    clone->mWord = mWord;
}

bool cResourceNode::IsKindOf(uint32_t iid)
{
    if (iid == cIResourceNode::kIid || iid == cISGResource::kIid || iid == kIidPersistResource2) return true;
    return Node::IsKindOf(iid);
}

uint32_t cResourceNode::Accept(cICompositionTreeTraversal *traversal) { return traversal->VisitResource(this); }
uint32_t cResourceNode::AcceptReadOnly(cICompositionTreeTraversal *traversal) { return traversal->VisitResource(this); }

void *cResourceNode::MakeIterator43(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator43(&mImpl, out, this, a, b);
}
void *cResourceNode::MakeIterator44(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator44(&mImpl, out, this, a, b);
}
void *cResourceNode::MakeIterator51(void *out, uint32_t a) { return node_family::BuildIterator51(&mImpl, out, a); }

cISGResource *cResourceNode::GetResourceConst() { return static_cast<cISGResource *>(this); }
cISGResource *cResourceNode::GetResource() { return static_cast<cISGResource *>(this); }
void cResourceNode::Slot63(uint32_t value) { mWord = value; }
uint32_t cResourceNode::Slot64() { return mWord; }

void cResourceNode::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    if (reader->GetBool()) {
        cSGResource::Read(reader);
        mImpl.Read(this, reader);
    } else {
        mImpl.cObjectGraphNodeImpl::Read(this, reader);
    }

    if (cIResourceNode *const old = mSource) {
        mSource = nullptr;
        old->Release();
    }
    reader->GetObjectRef(cIResourceNode::kIid, &mSource);
    if (mSource) {
        // The source's children, each cloned whole (mode 1) and added (slot 45, at the front).
        const cChildRange range = mSource->GetChildren();
        for (cICompositionTreeNode *child = range.mCurrent; child != range.mEnd; child = child->GetNextSibling()) {
            cICompositionTreeNode *copy = nullptr;
            child->Clone(1, cICompositionTreeNode::kIid, reinterpret_cast<void **>(&copy), nullptr);
            AddChild(copy);
            if (copy) copy->Release();
        }
    }
    mWord = reader->GetUint32();
    if (info.mVersion < kVersion) {
        nTSSG::SGLocalString dropped; // an old field, read and not kept
        reader->GetGZStr(&dropped.value);
    }
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cResourceNode::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cResourceNode");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

void cResourceNode::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cResourceNode::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    writer->SetUint8(mSource == nullptr);
    if (!mSource) {
        cSGResource::WriteResource(writer);
        mImpl.Write(this, writer);
    } else {
        mImpl.cObjectGraphNodeImpl::Write(this, writer);
    }
    writer->WriteFlaggedObject(mSource ? mSource->GetResource() : nullptr);
    writer->SetUint32(mWord);
}

uint32_t cResourceNode::GetOwnerInfo() { return mSource ? Node::GetOwnerInfo() : cSGResource::GetOwnerInfo(); }

uint32_t cResourceNode::GetRefCount() { return this->Node::mRefCount & nGZEvent::behavior_mt::kCountMask; }

bool cResourceNode::SetSegment(cIGZUnknown *segment)
{
    if (cIResourceNode *const old = mSource) {
        mSource = nullptr;
        old->Release();
    }
    return cSGResource::SetSegment(segment);
}

const void *cResourceNode::GetRecord18() { return "_cres"; }
const void *cResourceNode::GetRecord19() { return "5cr"; }

} // namespace nGZSceneGraph
