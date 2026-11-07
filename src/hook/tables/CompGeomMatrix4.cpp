#include "hook/hooks.h"
#include "compgeom/Frames.h"
#include "compgeom/Matrix4.h"
#include "compgeom/SeparatingAxes.h"

// The 4x4 matrices and the frame / rigid members that build them (scenegraph lane TR, src/compgeom/Matrix4.h): called
// from the shader constant sources, the texture matrix, the octree and the transform nodes, hooked at their RVAs.
namespace {

const HookSpec kHooks[] = {
    {"nGZCompGeom_FrameToMatrix4", 0x19c0e0, 32, 0x9877ce6a, MemberAddress(&nGZCompGeom::FrameMath::ToMatrix4)},
    {"nGZCompGeom_RigidToMatrix4", 0x19c1c0, 32, 0xaf9a168c, MemberAddress(&nGZCompGeom::RigidTransform3::ToMatrix4)},
    {"nGZCompGeom_RigidRotateVector", 0x19c800, 19, 0xf5cf4a6c, MemberAddress(&nGZCompGeom::RigidTransform3::RotateVector)},
    {"nGZCompGeom_FrameComposeRigid", 0x19be70, 32, 0xd3e875cb, MemberAddress(&nGZCompGeom::FramePlacement::ComposeRigid)},
    {"nGZNumerics_Matrix4Multiply", 0x1a5eb0, 32, 0x62fecb1f, reinterpret_cast<void *>(&nGZCompGeom::Matrix4Multiply)},
    {"nGZNumerics_Matrix4Inverse", 0x1a6410, 32, 0xe3cd7de9, reinterpret_cast<void *>(&nGZCompGeom::Matrix4Inverse)},
    {"nGZNumerics_Matrix4Translation", 0x1a5870, 32, 0x24e22a69, reinterpret_cast<void *>(&nGZCompGeom::Matrix4Translation)},
    {"nGZNumerics_Matrix4FromRotationScaleTranslation", 0x1a7200, 32, 0x21c8bf2b,
     reinterpret_cast<void *>(&nGZCompGeom::Matrix4FromRotationScaleTranslation)},
    {"nGZNumerics_LuDecompose4", 0x1a7c50, 32, 0xd37d0e85, MemberAddress(&nGZCompGeom::LuDecomposition4::Decompose)},
    {"nGZNumerics_LuSolve4", 0x1a8030, 32, 0x518a8cad, MemberAddress(&nGZCompGeom::LuDecomposition4::Solve)},
    {"nGZNumerics_EulerAnglesToMatrix3", 0x1a5660, 32, 0xc4c1c549, reinterpret_cast<void *>(&nGZCompGeom::EulerAnglesToMatrix3)},
};
REGISTER_HOOKS(kHooks);

} // namespace
