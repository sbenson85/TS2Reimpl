#include "sg/SGNodeCopies.h"

#include <new>

namespace nGZSceneGraph::node_copies {

cCompositionTreeNode *__fastcall ConstructCompositionTreeNode(void *memory, void *)
{
    return ::new (memory) cCompositionTreeNode;
}

cTransformNode *__fastcall ConstructTransformNode(void *memory, void *)
{
    return ::new (memory) cTransformNode;
}

} // namespace nGZSceneGraph::node_copies
