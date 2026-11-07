#pragma once
// nGZSceneGraph's world-transform refresh (lane OP, scenegraph N0 part 3 step 4): 0xa39b70, cdecl on a node, called by
// the bounded nodes' bound queries and their slot 54 (and 40-odd game functions) when the node's flag 12 (its cached
// world transform is stale) is set. Hooked; its helpers are in effect through it: the recursion 0xa39a10 and the three
// list appends 0xa39500 (roots), 0xa39590 (content nodes), 0xa39430 (limits). The game keeps its work lists in a
// function-local static (0x10f5fac-0x10f5fe4, guard 0x10f5fe8, freed by the atexit 0xd95bd0); ours are a static of ours.
//
// What it does:
//   1. Climbs from the node to the topmost ancestor still stale: while the parent (slot 33) has flag 12, the parent.
//   2. That node's world transform: its parent's (slot 54; the identity without a parent) through the node's slot 57
//      (ToParent1c). It becomes the first root: {node, world}.
//   3. For each root in order (the list grows while it is walked): a root whose limit (slot 55) is 0 starts the limit
//      list with {0, its world}; then the walk below (0xa39a10), then every content node it listed gets its slot 60
//      (OnContentChanged: its world transform, the limit list's records, their count); the limit and content lists are
//      emptied. At the end the root list is emptied.
//   The walk below a node with its world transform: the node is a content node (listed with its transform) when its
//   flag 4 is set; otherwise its slot 60 runs at once (the transform, 0, 0). Then each child (slot 41's range, slot 37
//   onwards) gets its world transform through its slot 57 from the node's; a child whose limit is 0 becomes a root
//   (walked later, from step 3); else a child whose limit is not 0x7fffffff adds {limit, transform} to the limit list,
//   and the walk goes on below it.
//
// The records a content node's slot 60 sees: its transform is a record's +4 (the record is {node, 7 floats}); the
// limit list is an array of {limit, 7 floats} records (32 bytes each).
#include "sg/SGNodeInterfaces.h"

#include <cstdint>

namespace nGZSceneGraph {

// 0xa39b70.
void RefreshWorldTransforms(cICompositionTreeNode *node);

} // namespace nGZSceneGraph
