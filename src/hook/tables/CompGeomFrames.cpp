#include "hook/hooks.h"
#include "compgeom/Frames.h"

// nGZNumerics' frame members (lane PC, src/compgeom/Frames.h): called from the compgeom shapes, the scenegraph and the
// renderer, hooked at their RVAs.
namespace {

const HookSpec kHooks[] = {
    {"Frame_VectorTo", 0x19c770, 32, 0xd2b6dd48, MemberAddress(&nGZCompGeom::FrameMath::VectorTo)},
    {"Frame_InverseVectorTo", 0x19c250, 32, 0x60095187, MemberAddress(&nGZCompGeom::FrameMath::InverseVectorTo)},
    {"Frame_InversePointTo", 0x19c3b0, 32, 0xef6308f5, MemberAddress(&nGZCompGeom::FrameMath::InversePointTo)},
    {"Frame_InverseMatrix", 0x19cb30, 32, 0x16b7d0bb, MemberAddress(&nGZCompGeom::FrameMath::InverseMatrix)},
    {"Frame_Scale", 0x19cf10, 32, 0xb4e3260e, MemberAddress(&nGZCompGeom::FrameMath::Scale)},
    {"TransposeMatrix", 0x1a6590, 32, 0x5d75ceb5, reinterpret_cast<void *>(&nGZCompGeom::TransposeMatrix)},
};
REGISTER_HOOKS(kHooks);

} // namespace
