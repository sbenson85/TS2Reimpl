#include "hook/hooks.h"
#include "compgeom/Frames.h"
#include "compgeom/Rigid.h"

// The rigid transform, the quaternion helpers, the cuboid boxes and the LU inverse (lane PH, src/compgeom/Rigid.h,
// Frames.h): called from the shapes, the scenegraph and the octree, hooked at their RVAs.
namespace {

const HookSpec kHooks[] = {
    {"nGZCompGeom_RigidTransformPoint", 0x19c900, 32, 0x61cbf481, MemberAddress(&nGZCompGeom::RigidTransform3::TransformPoint)},
    {"nGZCompGeom_RigidInverseTransformPoint", 0x19c6d0, 32, 0x4f6828d0, MemberAddress(&nGZCompGeom::RigidTransform3::InverseTransformPoint)},
    {"nGZCompGeom_RigidInverseTransformVector", 0x19c390, 19, 0xcc4276a9, MemberAddress(&nGZCompGeom::RigidTransform3::InverseTransformVector)},
    {"nGZCompGeom_RigidToMatrix", 0x19c230, 23, 0x2d44d4d4, MemberAddress(&nGZCompGeom::RigidTransform3::ToMatrix)},
    {"nGZNumerics_QuaternionToMatrix", 0x1a77f0, 32, 0x6d10f00d, reinterpret_cast<void *>(&nGZCompGeom::QuaternionToMatrix)},
    {"nGZNumerics_RotateByInverseQuaternion", 0x1a6ad0, 32, 0x8975536f, reinterpret_cast<void *>(&nGZCompGeom::RotateByInverseQuaternion)},
    {"nGZCompGeom_Dot87", 0x18fbf0, 29, 0x8e4a81ca, reinterpret_cast<void *>(&nGZCompGeom::Dot87)},
    {"nGZCompGeom_BoundBoxTransform", 0x19d730, 32, 0x45cf5025, MemberAddress(&nGZCompGeom::BoundBox::Transform)},
    {"nGZCompGeom_BoundBoxCorner", 0x19d390, 32, 0x4ca1070d, MemberAddress(&nGZCompGeom::BoundBox::Corner)},
    {"nGZCompGeom_CuboidBoxInFrame", 0x18ff90, 32, 0x822d9c8e, MemberAddress(&nGZCompGeom::Cuboid::BoxInFrame)},
    {"nGZCompGeom_CuboidBoxInRigid", 0x190170, 32, 0x3c6da44b, MemberAddress(&nGZCompGeom::Cuboid::BoxInRigid)},
    {"nGZCompGeom_RigidCompose", 0x19c080, 32, 0xa4257bc8, MemberAddress(&nGZCompGeom::RigidTransform3::Compose)},
    {"nGZNumerics_InverseMatrix3", 0x1a62e0, 32, 0x9ced05d8, reinterpret_cast<void *>(&nGZCompGeom::InverseMatrix3)},
    {"nGZNumerics_LuDecompose3", 0x1a7aa0, 32, 0xe9f09608, MemberAddress(&nGZCompGeom::LuDecomposition3::Decompose)},
    {"nGZNumerics_LuSolve3", 0x1a7e50, 32, 0xd7fda07c, MemberAddress(&nGZCompGeom::LuDecomposition3::Solve)},
    {"Frame_SetRotation", 0x19d0d0, 32, 0xcfc13976, MemberAddress(&nGZCompGeom::FrameMath::SetRotation)},
    {"nGZCompGeom_RigidInvert", 0x19cd80, 32, 0x7c8b81e7, MemberAddress(&nGZCompGeom::RigidTransform3::Invert)},
    {"nGZSceneGraph_cRigidTransform_CarryPlane", 0x19ca50, 32, 0x0cd24ed0, MemberAddress(&nGZCompGeom::RigidTransform3::TransformPlane)},
    {"nGZCompGeom_RigidNearlyEqual", 0xae79e0, 32, 0x224ca6c4, reinterpret_cast<void *>(&nGZCompGeom::RigidNearlyEqual)},
    {"FUN_a019d4a0", 0x19d4a0, 32, 0x098fe834, MemberAddress(&nGZCompGeom::BoundBox::Extend)},
};
REGISTER_HOOKS(kHooks);

} // namespace
