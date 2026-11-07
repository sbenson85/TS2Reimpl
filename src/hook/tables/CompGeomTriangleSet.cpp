#include "hook/hooks.h"
#include "compgeom/TriangleSet.h"

// nGZCompGeom's triangle sets (lane PC, src/compgeom/TriangleSet.h). Standalone shape: the director's factory hands out
// our cTriangleSetShape, so the slot bodies of both vtables are in effect through callers (trap: this table's
// TriangleSetShape_Create). The set's own functions and the two box helpers have callers all over the game: hooked at
// their RVAs.
namespace {

const HookSpec kHooks[] = {
    {"TriangleSetShape_Create", 0x196560, 32, 0xcdded22a, reinterpret_cast<void *>(&nGZCompGeom::cTriangleSetShape::Create)},
    {"TriangleSet_Resize", 0x195af0, 32, 0x454ebe97, MemberAddress(&nGZCompGeom::TriangleSet::Resize)},
    {"TriangleSet_SetVertex", 0x196d00, 32, 0x2db0b810, MemberAddress(&nGZCompGeom::TriangleSet::SetVertex)},
    {"TriangleSet_SetTriangle", 0x196cc0, 32, 0xa0242fe5, MemberAddress(&nGZCompGeom::TriangleSet::SetTriangle)},
    {"TriangleSet_Build", 0x196300, 10, 0x6de38a02, MemberAddress(&nGZCompGeom::TriangleSet::Build)},
    {"TriangleSet_Refit", 0x1970f0, 16, 0x5569c156, MemberAddress(&nGZCompGeom::TriangleSet::Refit)},
    {"BoundBox_Grow", 0x19d430, 32, 0x098fe834, MemberAddress(&nGZCompGeom::BoundBox::Grow)},
    {"Cuboid_BoxAt", 0x1902e0, 32, 0x01e4f704, MemberAddress(&nGZCompGeom::Cuboid::BoxAt)},
};
REGISTER_HOOKS(kHooks);

} // namespace
