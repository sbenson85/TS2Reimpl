#pragma once
// The game's per-instantiation copies of the node templates (lane SY, scenegraph F1b): cObjectGraphNodeT,
// cCompositionTreeNodeT, cTransformNodeT, ... instantiated once per interface, each with its constructor and destructor
// chain, its deleting-destructor stubs and its slot bodies. Ours are the templates in sg/SGNodeTemplates.h, instantiated
// by our classes in the standalone shape.
//
// The switch for an instantiation whose concrete class is ours: its complete constructor, hooked to build our class in
// place. Then no object carries the game's vtables of that instantiation (the constructor chain is the only place they
// are stored), and every body only those vtables and that chain reach is in effect through its callers
// (db/status/SGNodeCopies.jsonl). The constructors (thiscall: the memory in ecx, returns it):
//   0xa36900  cCompositionTreeNode  (callers: the factory 0xa37910, hooked; the Clones 0x25a050 / 0xa373c0 / 0xa6a750)
//   0xa3a000  cTransformNode        (callers: the factory 0xa3a6c0, hooked; the Clones 0x25a180 / 0xa3a3b0 / 0xa6a840)
// Not switched: cExplicitBoundNode's 0xa4b500, which nTSSG::cExplicitShearBoundNode's constructor 0x3cffc0 runs as its
// base before laying its own vtables (whose slots hold the game's explicit-bound bodies): building ours there would
// leave a game-derived class over our base, and the copies would stay reachable anyway.
#include "sg/cCompositionTreeNode.h"
#include "sg/cTransformNode.h"

namespace nGZSceneGraph::node_copies {

// Hook adapters: thiscall-compatible (the memory in ecx), each returns the memory with our class built in it
// (default-initialised: every member our constructors set, as the game's).
cCompositionTreeNode *__fastcall ConstructCompositionTreeNode(void *memory, void *);
cTransformNode *__fastcall ConstructTransformNode(void *memory, void *);

} // namespace nGZSceneGraph::node_copies
