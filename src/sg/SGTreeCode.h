// nGZSceneGraph's tree code (lane OM, scenegraph N0 part 3 step 4): what a composition node being connected, about to
// be disconnected, moved, or having its bound flag changed does to the nodes below it. cdecl functions on the node.
//
// The node flags they use (cICompositionTreeNode slots 29 SetFlag, 48 TestFlag, 49 GetFlags):
//   1  the node is bounded (a cIBoundedNode: GetHolder, slot 69)
//   6  the node is a partition (a cIPartitionNode: Clear 77; its holder's RemovePartition 78 instead of RemoveNode 79)
//   7  stops the climb to the propagation root
//   12 the world transform is stale (set on every node a propagation reaches)
//   13 the node is queued on its scene (cISceneNode::QueueNode 95 / UnqueueNode 96)
// A node's scene is its slot 32 (the scene node above it, or 0); a propagation hands the root's scene to every node
// below it. Each queue change is announced to the node's listeners (kQueuedEvent, a cNullEvent).
//
// Hooked: the four entry points (each called by game code and by ours). FindPropagationRoot and the three recursive
// propagations are in effect through them. The world-transform refresh 0xa39b70 is ours too
// (sg/SGWorldTransformRefresh.h).
#pragma once

#include <cstdint>

namespace nGZSceneGraph {

class cICompositionTreeNode;
struct cRigidTransform;

inline constexpr uint32_t kQueuedEvent = 0xa884a15; // a node joined or left its scene's queue

// 0xa39620: where a change to `node` propagates from. `node` itself, unless its limit (slot 55) is neither 0 nor
// 0x7fffffff and it has a parent: then the nearest ancestor whose limit is 0, if there is one and its flag 7 is clear.
cICompositionTreeNode *FindPropagationRoot(cICompositionTreeNode *node);

// 0xa396d0: `node` was connected: from its propagation root down, every node is marked stale (flag 12), each bounded
// node not yet queued is queued on the root's scene, and each partition is cleared (0xa39740).
void NodeConnected(cICompositionTreeNode *node);

// 0xa39710: `node` is about to be disconnected: from its propagation root down, every node is marked stale, and each
// bounded node leaves its scene's queue (if queued) and its holder (0xa39820).
void NodeDisconnecting(cICompositionTreeNode *node);

// 0xa39690 (the thunk 0xa39700 jumps to it): `node`'s transform or limit changed. Unless it is already stale, the
// connection propagation runs from its propagation root, skipping every subtree whose top is already stale (0xa39920).
void NodeTransformChanged(cICompositionTreeNode *node);

// 0xa39ae0: `node`'s bound flag changed: unless already queued, it is queued on its scene (if it has one).
void NodeBoundFlagChanged(cICompositionTreeNode *node);

// Lane VS: the two limit walks the shape-reference node's material block runs (0xa589a0), hooked. The limit (slot 55)
// numbers the node's transform in the block.
//
// 0xae7aa0: the nearest of `node` and its ancestors (slot 33) whose limit is 0, unless its flag 7 is set; null when there
// is none, or for a null node.
cICompositionTreeNode *FindLimitRoot(cICompositionTreeNode *node);

// 0xae7910: `transform` stored at transforms[limit] when the node's limit is below `count` (0x7fffffff never is); then,
// for each child (slot 41's range, slot 37 to the next) whose limit is not 0, the child's transform (slot 57 of `transform`)
// gathered from the child the same way. Recursive.
void GatherLimitTransforms(cRigidTransform *transforms, uint32_t count, cICompositionTreeNode *node,
                           const cRigidTransform *transform);

} // namespace nGZSceneGraph
