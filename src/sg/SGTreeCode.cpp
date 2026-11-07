// nGZSceneGraph's tree code (lane OM). See the header.
#include "sg/SGTreeCode.h"

#include "game/game.h"
#include "misc/SmallVirtuals.h" // nGZEvent::cNullEvent::kVTable
#include "gz/cEventFaces.h"    // nGZEvent::NullEventVTable (the owned word)
#include "sg/SGNodeInterfaces.h"

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kBoundedFlag = 1, kPartitionFlag = 6, kRootStopFlag = 7, kStaleFlag = 12, kQueuedFlag = 13;
constexpr uint32_t kBounded = 1u << kBoundedFlag, kPartition = 1u << kPartitionFlag, kStale = 1u << kStaleFlag,
                   kQueued = 1u << kQueuedFlag;
constexpr uint32_t kNoLimit = 0x7fffffff;

cISceneNode *SceneOf(cICompositionTreeNode *node)
{
    return reinterpret_cast<cISceneNode *>(static_cast<uintptr_t>(node->GetFromParent32()));
}

// The flag set or cleared, then the listeners told (a cNullEvent body).
void SetQueued(cICompositionTreeNode *node, bool queued)
{
    node->SetFlag(kQueuedFlag, queued);
    const void *nullEvent = nGZEvent::NullEventVTable();
    node->NotifyListeners(kQueuedEvent, 0, &nullEvent);
}

// A bounded node that is not queued yet goes onto the scene's queue.
void QueueIfBounded(cICompositionTreeNode *node, uint32_t flags, cISceneNode *scene)
{
    if (!scene || (flags & (kBounded | kQueued)) != kBounded) return;
    scene->QueueNode(static_cast<cIBoundedNode *>(node));
    SetQueued(node, true);
}

// The node's subtree, depth first: `visit` on each child (cursor range from slot 42, the next through slot 38).
template <class Visit> void ForEachChild(cICompositionTreeNode *node, Visit &&visit)
{
    const cChildRange children = node->GetChildren();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSibling())
        visit(child);
}

// 0xa39740
void PropagateConnected(cICompositionTreeNode *node, cISceneNode *scene)
{
    node->SetFlag(kStaleFlag, true);
    const uint32_t flags = node->GetFlags();
    QueueIfBounded(node, flags, scene);
    if (flags & kPartition) static_cast<cIPartitionNode *>(node)->Clear();
    ForEachChild(node, [scene](cICompositionTreeNode *child) { PropagateConnected(child, scene); });
}

// 0xa39820
void PropagateDisconnecting(cICompositionTreeNode *node, cISceneNode *scene)
{
    node->SetFlag(kStaleFlag, true);
    const uint32_t flags = node->GetFlags();
    if (flags & kBounded) {
        cIBoundedNode *const bounded = static_cast<cIBoundedNode *>(node);
        if (scene && (flags & kQueued)) {
            scene->UnqueueNode(bounded);
            SetQueued(node, false);
        }
        if (cIPartitionNode *const holder = static_cast<cIPartitionNode *>(bounded->GetHolder())) {
            if (flags & kPartition)
                holder->RemovePartition(static_cast<cIPartitionNode *>(node));
            else
                holder->RemoveNode(bounded);
        }
    }
    ForEachChild(node, [scene](cICompositionTreeNode *child) { PropagateDisconnecting(child, scene); });
}

// 0xa39920
void PropagateTransform(cICompositionTreeNode *node, cISceneNode *scene)
{
    const uint32_t flags = node->GetFlags();
    if (flags & kStale) return;
    node->SetFlag(kStaleFlag, true);
    QueueIfBounded(node, flags, scene);
    if (flags & kPartition) static_cast<cIPartitionNode *>(node)->Clear();
    ForEachChild(node, [scene](cICompositionTreeNode *child) { PropagateTransform(child, scene); });
}

} // namespace

cICompositionTreeNode *FindPropagationRoot(cICompositionTreeNode *node)
{
    const uint32_t limit = node->GetLimit55();
    if (limit == kNoLimit || limit == 0) return node;
    cICompositionTreeNode *ancestor = node->GetParent();
    if (!ancestor) return node;
    while (ancestor->GetLimit55() != 0) {
        ancestor = ancestor->GetParent();
        if (!ancestor) return node;
    }
    return ancestor->TestFlag(kRootStopFlag) ? node : ancestor;
}

void NodeConnected(cICompositionTreeNode *node)
{
    cICompositionTreeNode *const root = FindPropagationRoot(node);
    PropagateConnected(root, SceneOf(root));
}

void NodeDisconnecting(cICompositionTreeNode *node)
{
    cICompositionTreeNode *const root = FindPropagationRoot(node);
    PropagateDisconnecting(root, SceneOf(root));
}

void NodeTransformChanged(cICompositionTreeNode *node)
{
    if (node->TestFlag(kStaleFlag)) return;
    cICompositionTreeNode *const root = FindPropagationRoot(node);
    PropagateTransform(root, SceneOf(root));
}

void NodeBoundFlagChanged(cICompositionTreeNode *node)
{
    if (node->TestFlag(kQueuedFlag)) return;
    cISceneNode *const scene = SceneOf(node);
    if (!scene) return;
    scene->QueueNode(static_cast<cIBoundedNode *>(node));
    SetQueued(node, true);
}

cICompositionTreeNode *FindLimitRoot(cICompositionTreeNode *node)
{
    for (; node; node = node->GetParentConst())
        if (node->GetLimit55() == 0) return node->TestFlag(kRootStopFlag) ? nullptr : node;
    return nullptr;
}

void GatherLimitTransforms(cRigidTransform *transforms, uint32_t count, cICompositionTreeNode *node,
                           const cRigidTransform *transform)
{
    const uint32_t limit = node->GetLimit55();
    if (limit != kNoLimit && limit < count) transforms[limit] = *transform;
    const cChildRange children = node->GetChildrenConst();
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSiblingConst()) {
        if (child->GetLimit55() == 0) continue;
        cRigidTransform childTransform;
        child->ToParent1c(&childTransform, transform);
        GatherLimitTransforms(transforms, count, child, &childTransform);
    }
}

} // namespace nGZSceneGraph
