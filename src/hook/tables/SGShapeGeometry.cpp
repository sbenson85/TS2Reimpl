#include "hook/hooks.h"
#include "sg/SGShapeGeometry.h"

namespace {

// Slot bodies of nGZSceneGraph::cGeometryNode and nGZSceneGraph::cShape (src/sg/SGShapeGeometry.h). The thunks
// and bodies are reached through the vtables; the 8-byte thunk 0xa431c0 is hooked as its own body.
const HookSpec kHooks[] = {
    {"SGShapeGeometry_cGeometryNode_vf8_6", 0xa431c0, 8, 0xe9935d27, MemberAddress(&nGZSceneGraph::cGeometryNodeSecondary8::VirtualBaseOfResource)},
    {"SGShapeGeometry_cGeometryNode_vf45", 0xa42980, 19, 0xdeb886c6, MemberAddress(&nGZSceneGraph::cGeometryNodeObject::Dispatch8OfElement)},
    {"SGShapeGeometry_cGeometryNode_vf47", 0xa42800, 21, 0x8ef6ba52, MemberAddress(&nGZSceneGraph::cGeometryNodeObject::Dispatch8OfWordAt10c)},
    {"SGShapeGeometry_cGeometryNode_vf48", 0xa427e0, 21, 0x85fc9ae3, MemberAddress(&nGZSceneGraph::cGeometryNodeObject::Dispatch8OfWordAt118)},
    {"SGShapeGeometry_cGeometryNode_vf46", 0xa429a0, 15, 0xfe3ee3b5, MemberAddress(&nGZSceneGraph::cGeometryNodeObject::ElementWordAt38)},
    {"SGShapeGeometry_cGeometryNode_vf64_5", 0xa42970, 11, 0xdf633473, MemberAddress(&nGZSceneGraph::cGeometryNodeRenderSubobject::ElementPointer)},
    {"SGShapeGeometry_cGeometryNode_vf31", 0xa43510, 29, 0x672348a8, MemberAddress(&nGZSceneGraph::cGeometryNodeObject::SetField124)},
    {"SGShapeGeometry_cGeometryNode_vf38", 0xa43530, 32, 0x5258c0b3, MemberAddress(&nGZSceneGraph::cGeometryNodeObject::ReplaceRefE0)},
    {"SGShapeGeometry_cGeometryNode_vf35", 0xa42ac0, 32, 0xb03ccfef, MemberAddress(&nGZSceneGraph::cGeometryNodeObject::AnyElementAccepts)},
    {"SGShapeGeometry_cGeometryNode_vf64_7", 0xa423c0, 32, 0x4e883043, MemberAddress(&nGZSceneGraph::cGeometryNodeRenderSubobject::ForwardElement)},
    {"SGShapeGeometry_cShape_vf44_49", 0xa5d420, 15, 0xce2d2fbd, MemberAddress(&nGZSceneGraph::cShapeIShapeSubobject::FirstRecordWord)},
    {"SGShapeGeometry_cShape_vf44_58", 0xa5e170, 32, 0x408dba79, MemberAddress(&nGZSceneGraph::cShapeIShapeSubobject::QueryPair)},
};
REGISTER_HOOKS(kHooks);

} // namespace
