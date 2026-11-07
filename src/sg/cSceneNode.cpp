// nGZSceneGraph::cSceneNode (lane OK). Layout, faces and the switch: see the header.
#include "sg/cSceneNode.h"

#include "game/original.h"
#include "gz/cIGZFrameWork.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "ifaces/cISceneGraphSystemPrivate.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h"     // GetServiceObject: the framework
#include "sg/SGWorldTransformRefresh.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName
#include "sg/cTransformNode.h"       // node_family: the iterator builders

#include <algorithm>
#include <cfloat>
#include <new>

namespace nGZSceneGraph {

static_assert(sizeof(cSceneNode) == cSceneNode::kSize, "cSceneNode size");
static_assert(offsetof(cSceneNode, mInQuery) == 0x98, "cSceneNode mInQuery");
static_assert(offsetof(cSceneNode, mPartitions) == 0x9c, "cSceneNode mPartitions");
static_assert(offsetof(cSceneNode, mFreePartitions) == 0xa8, "cSceneNode mFreePartitions");
static_assert(offsetof(cSceneNode, mNodes) == 0xb4, "cSceneNode mNodes");
static_assert(offsetof(cSceneNode, mFreeNodes) == 0xc0, "cSceneNode mFreeNodes");
static_assert(offsetof(cSceneNode, mPendingPartitions) == 0xcc, "cSceneNode mPendingPartitions");
static_assert(offsetof(cSceneNode, mPendingNodes) == 0xd8, "cSceneNode mPendingNodes");

namespace {

// The scene graph system (counted, in ref.mObject), or null without a framework or a system.
void GetSystem(RZAutoRef &ref)
{
    if (auto *framework = static_cast<cIGZFrameWork *>(GetServiceObject()))
        framework->GetSystemService(cSceneNode::kSystemService, cSceneNode::kSystemIid, &ref.mObject);
}

ifaces::cISceneGraphSystemPrivate *AsSystem(const RZAutoRef &ref)
{
    return static_cast<ifaces::cISceneGraphSystemPrivate *>(ref.mObject);
}

uint32_t Index(size_t value) { return static_cast<uint32_t>(value); }

} // namespace

// ---- construction ----

cSceneNode::cSceneNode()
{
    mImpl.SetFlag(7, true);
    mImpl.SetFlag(cBoundedNodeImpl::kStaleFlag, false);
    Node::SetLimit(0);

    RZAutoRef system{nullptr};
    GetSystem(system);
    // ORIGINAL-BUG: the constructor calls the system without a null check (the destructor has one); the system is there
    // from boot, so the game never meets a null one. So does this.
    AsSystem(system)->AddSceneNode(static_cast<cISceneNode *>(this));
    system.ReleaseObject();
}

cSceneNode::~cSceneNode()
{
    cSceneNode::Clear();
    // The tree code takes each queued node off its queue (through slot 96 on the node's root); the loops end when it has.
    while (!mPendingPartitions.empty()) NodeDisconnecting(mPendingPartitions.front());
    while (!mPendingNodes.empty()) NodeDisconnecting(mPendingNodes.front());

    RZAutoRef system{nullptr};
    GetSystem(system);
    if (system.mObject) AsSystem(system)->RemoveSceneNode(static_cast<cISceneNode *>(this));
    system.ReleaseObject();
}

// (The original's operator new throws on failure, so its null branch is dead.)
cSceneNode *__cdecl cSceneNode::Create() { return new cSceneNode; }

uint32_t cSceneNode::GetClassId() { return kClassId; }

// ---- cIGZUnknown, cIObjectGraphNode, cICompositionTreeNode ----

bool cSceneNode::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == cISceneNode::kIid) {
        *out = static_cast<cISceneNode *>(this);
        AddRef();
        return true;
    }
    if (Node::QueryInterface(iid, out)) return true;
    return cSGResource::QueryInterface(iid, out);
}

uint32_t cSceneNode::AddRef() { return Node::AddRef(); }
uint32_t cSceneNode::Release() { return Node::Release(); }

bool cSceneNode::IsKindOf(uint32_t iid) { return iid == cISceneNode::kIid || Node::IsKindOf(iid); }

uint32_t cSceneNode::Accept(cICompositionTreeTraversal *traversal) { return traversal->VisitScene(this); }
uint32_t cSceneNode::AcceptReadOnly(cICompositionTreeTraversal *traversal) { return traversal->VisitScene(this); }

uint32_t cSceneNode::GetFromParent31() { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cISceneNode *>(this))); }
uint32_t cSceneNode::GetFromParent32() { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cISceneNode *>(this))); }

void *cSceneNode::MakeIterator43(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator43(&mImpl, out, this, a, b);
}
void *cSceneNode::MakeIterator44(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator44(&mImpl, out, this, a, b);
}
void *cSceneNode::MakeIterator51(void *out, uint32_t a) { return node_family::BuildIterator51(&mImpl, out, a); }

// ---- cIBoundedNode ----

cBoundBox cSceneNode::GetBoundBoxIn(const cTransform34 *) { return GetBoundBox(); }

cBoundBox cSceneNode::GetBoundBox()
{
    cBoundBox box;
    for (int i = 0; i < 3; ++i) {
        box.mMin[i] = -FLT_MAX;
        box.mMax[i] = FLT_MAX;
    }
    return box;
}

bool cSceneNode::FirstHitLinear(float *t, nGZCompGeom::cILinear3 *)
{
    *t = 0.0f;
    return true;
}

// ---- cIPartitionNode ----

void cSceneNode::Clear()
{
    for (size_t i = 0; i < mPartitions.size(); ++i) {
        if (cIPartitionNode *const partition = mPartitions[i]) {
            RemovePartition(partition);
            NodeBoundFlagChanged(partition);
        }
    }
    for (size_t i = 0; i < mNodes.size(); ++i) {
        if (cIBoundedNode *const node = mNodes[i]) {
            RemoveNode(node);
            NodeBoundFlagChanged(node);
        }
    }
}

void cSceneNode::RemovePartition(cIPartitionNode *partition)
{
    const uint32_t index = partition->GetHolderData();
    mFreePartitions.push_back(index);
    mPartitions[index] = nullptr;
    partition->SetHolder(nullptr, 0);
}

bool cSceneNode::RemoveNode(cIBoundedNode *node)
{
    const uint32_t index = node->GetHolderData();
    mFreeNodes.push_back(index);
    mNodes[index] = nullptr;
    node->SetHolder(nullptr, 0);
    return true;
}

bool cSceneNode::AddPartition(cIPartitionNode *partition)
{
    if (partition == static_cast<cIPartitionNode *>(this)) return false;
    for (size_t i = 0; i < mPartitions.size(); ++i) {
        if (mPartitions[i] && mPartitions[i]->AddPartition(partition)) return true;
    }
    // The new partition takes in the sub-partitions it accepts.
    for (size_t i = 0; i < mPartitions.size(); ++i) {
        cIPartitionNode *const sub = mPartitions[i];
        if (!sub) continue;
        sub->SetHolder(nullptr, 0);
        if (partition->AddPartition(sub)) {
            mPartitions[i] = nullptr;
            mFreePartitions.push_back(Index(i));
        } else {
            sub->SetHolder(this, Index(i));
        }
    }
    if (mFreePartitions.empty()) {
        mPartitions.push_back(partition);
        partition->SetHolder(this, Index(mPartitions.size() - 1));
    } else {
        mPartitions[mFreePartitions.back()] = partition;
        partition->SetHolder(this, mFreePartitions.back());
        mFreePartitions.pop_back();
    }
    for (size_t i = 0; i < mNodes.size(); ++i) {
        if (mNodes[i]) NodeBoundFlagChanged(mNodes[i]);
    }
    return true;
}

bool cSceneNode::UpdateNode(cIBoundedNode *node)
{
    const uint32_t index = node->GetHolderData();
    node->SetHolder(nullptr, 0);
    for (size_t i = 0; i < mPartitions.size(); ++i) {
        if (mPartitions[i] && mPartitions[i]->InsertNode(node)) {
            // ORIGINAL-BUG: the node's index goes on the sub-partitions' free list, not the nodes' (0xa4ee17 pushes onto
            // +0xa8). The node slot stays empty for good, and a later AddPartition may place a sub-partition at an index
            // that is in use.
            mFreePartitions.push_back(index);
            mNodes[index] = nullptr;
            return true;
        }
    }
    node->SetHolder(this, index);
    return true;
}

bool cSceneNode::InsertNode(cIBoundedNode *node)
{
    for (size_t i = 0; i < mPartitions.size(); ++i) {
        if (mPartitions[i] && mPartitions[i]->InsertNode(node)) return true;
    }
    PlaceNode(node);
    return true;
}

void cSceneNode::PlaceNode(cIBoundedNode *node)
{
    if (mFreeNodes.empty()) {
        mNodes.push_back(node);
        node->SetHolder(this, Index(mNodes.size() - 1));
    } else {
        mNodes[mFreeNodes.back()] = node;
        node->SetHolder(this, mFreeNodes.back());
        mFreeNodes.pop_back();
    }
}

uint32_t cSceneNode::QueryShape(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cIBound *shape, uint32_t arg)
{
    const uint32_t shapeWord = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(shape));
    for (size_t i = 0; i < mPartitions.size(); ++i) {
        cIPartitionNode *const sub = mPartitions[i];
        if (sub && sub->IntersectsBound(shapeWord, arg) && sub->QueryShape(visitor, shape, arg) == 1) return 1;
    }
    for (size_t i = 0; i < mNodes.size(); ++i) {
        if (mNodes[i] && visitor->VisitShape(mNodes[i], shape, arg) == 1) break;
    }
    return 1;
}

uint32_t cSceneNode::QueryLinear(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cILinear3 *linear)
{
    for (size_t i = 0; i < mPartitions.size(); ++i) {
        cIPartitionNode *const sub = mPartitions[i];
        if (sub && sub->IntersectsLinear(linear) && sub->QueryLinear(visitor, linear) == 1) return 1;
    }
    for (size_t i = 0; i < mNodes.size(); ++i) {
        if (mNodes[i] && visitor->VisitLinear(mNodes[i], linear) == 1) break;
    }
    return 1;
}

void cSceneNode::RayCast(float *t, void *context, cINearerIntersectionQueryStrategy *visitor,
                         nGZCompGeom::cILinear3 *linear)
{
    for (size_t i = 0; i < mPartitions.size(); ++i) {
        cIPartitionNode *const sub = mPartitions[i];
        if (sub && sub->IntersectsLinear(linear)) sub->RayCast(t, context, visitor, linear);
    }
    for (size_t i = 0; i < mNodes.size(); ++i) {
        if (mNodes[i]) visitor->Visit(context, t, mNodes[i], linear);
    }
}

void cSceneNode::Cull(void *viewer)
{
    for (size_t i = 0; i < mPartitions.size(); ++i) {
        if (mPartitions[i]) mPartitions[i]->Cull(viewer);
    }
    for (size_t i = 0; i < mNodes.size(); ++i) {
        cIRenderableNode *const node = static_cast<cIRenderableNode *>(mNodes[i]);
        if (!node) continue;
        if ((node->GetFlags() & kDrawnFlags) == kDrawnFlags && (node->GetFlags() & kHiddenFlags) == 0) {
            cIVisibilityQueryFilter *const filter = *reinterpret_cast<cIVisibilityQueryFilter *const *>(
                static_cast<const uint8_t *>(viewer) + kViewerFilterOffset);
            if (!filter || filter->Accept(node, viewer)) node->Draw(viewer, 0);
        }
    }
}

// ---- cISceneNode ----

cISGResource *cSceneNode::GetResourceConst() { return static_cast<cISGResource *>(this); }
cISGResource *cSceneNode::GetResource() { return static_cast<cISGResource *>(this); }

void cSceneNode::FlushIfPending()
{
    if (!mPendingPartitions.empty() || !mPendingNodes.empty()) FlushPending();
}

uint32_t cSceneNode::SceneQueryShape(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cIBound *shape, uint32_t arg)
{
    FlushIfPending();
    mInQuery = 1;
    const uint32_t result = QueryShape(visitor, shape, arg);
    mInQuery = 0;
    return result;
}

uint32_t cSceneNode::SceneQueryLinear(cIIntersectionQueryStrategy *visitor, nGZCompGeom::cILinear3 *linear)
{
    FlushIfPending();
    mInQuery = 1;
    const uint32_t result = QueryLinear(visitor, linear);
    mInQuery = 0;
    return result;
}

uint32_t cSceneNode::SceneRayCast(float *t, cINearerIntersectionQueryStrategy *visitor, nGZCompGeom::cILinear3 *linear)
{
    FlushIfPending();
    mInQuery = 1;
    uint32_t context = 0; // the visitors' record: 0 until one of them sets it
    RayCast(t, &context, visitor, linear);
    mInQuery = 0;
    return context;
}

void cSceneNode::SceneCull(void *viewer)
{
    FlushIfPending();
    mInQuery = 1;
    Cull(viewer);
    mInQuery = 0;
}

void cSceneNode::FlushPending()
{
    if (mPendingPartitions.empty() && mPendingNodes.empty()) return;

    // Queued partitions are placed again (slot 80 always says false, so each is added afresh).
    for (size_t i = 0; i < mPendingPartitions.size(); ++i) {
        cIPartitionNode *const partition = static_cast<cIPartitionNode *>(mPendingPartitions[i]);
        if (partition->TestFlag(cBoundedNodeImpl::kStaleFlag)) RefreshWorldTransforms(partition);
        cIPartitionNode *const holder = static_cast<cIPartitionNode *>(partition->GetHolder());
        if (!holder || !holder->RemovePartitionFalse(partition)) AddPartition(partition);
        partition->SetFlag(13, false);
    }
    mPendingPartitions.clear();

    // Queued nodes: without the bound flag out of their partition; with it, a node that is not everywhere (slot 70) is
    // placed again (its partition first, else this node), one that is stays where it is (or comes here when it is in none).
    for (size_t i = 0; i < mPendingNodes.size(); ++i) {
        cIBoundedNode *const node = mPendingNodes[i];
        cIPartitionNode *const holder = static_cast<cIPartitionNode *>(node->GetHolder());
        if (!node->TestFlag(cBoundedNodeImpl::kBoundFlag)) {
            if (holder) holder->RemoveNode(node);
        } else if (!node->IsEverywhere()) {
            if (!holder || !holder->UpdateNode(node)) InsertNode(node);
        } else if (!holder) {
            PlaceNode(node);
        }
        if (node->TestFlag(cBoundedNodeImpl::kStaleFlag)) RefreshWorldTransforms(node);
        node->SetFlag(13, false);
    }
    mPendingNodes.clear();

    Compact(mPartitions, mFreePartitions);
    Compact(mNodes, mFreeNodes);
}

template <class T> void cSceneNode::Compact(std::vector<T *> &list, std::vector<uint32_t> &free)
{
    if (!(static_cast<float>(free.size()) * kCompactFactor > static_cast<float>(list.size()))) return;
    free.clear();
    size_t first = list.size();
    for (size_t i = list.size(); i-- > 0;) {
        if (!list[i]) {
            list.erase(list.begin() + static_cast<ptrdiff_t>(i));
            first = i;
        }
    }
    for (size_t i = first; i < list.size(); ++i) list[i]->SetHolder(this, Index(i));
}

void cSceneNode::QueueNode(cIBoundedNode *node)
{
    if (node->TestFlag(cBoundedNodeImpl::kReadSkipsBoundFlag)) {
        mPendingPartitions.push_back(node);
    } else {
        mPendingNodes.push_back(node);
    }
}

// (The node must be queued: the original erases at whatever find returns, the end included.)
void cSceneNode::UnqueueNode(cIBoundedNode *node)
{
    std::vector<cIBoundedNode *> &queue =
        node->TestFlag(cBoundedNodeImpl::kReadSkipsBoundFlag) ? mPendingPartitions : mPendingNodes;
    const auto found = std::find(queue.begin(), queue.end(), node);
    if (found != queue.end()) queue.erase(found);
}

// ---- the persist face ----

void cSceneNode::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cSGResource::Read(reader);
    Node::Read(reader);
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cSceneNode::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cSceneNode");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

void cSceneNode::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cSceneNode::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cSGResource::WriteResource(writer);
    Node::Write(writer);
}

uint32_t cSceneNode::GetOwnerInfo() { return cSGResource::GetOwnerInfo(); }

uint32_t cSceneNode::GetRefCount() { return this->Node::mRefCount & nGZEvent::behavior_mt::kCountMask; }

const void *cSceneNode::GetRecord18() { return "_scen"; }
const void *cSceneNode::GetRecord19() { return "5sc"; }

} // namespace nGZSceneGraph
