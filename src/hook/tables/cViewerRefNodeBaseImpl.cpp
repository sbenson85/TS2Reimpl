#include "hook/hooks.h"
#include "sg/cViewerRefNodeBaseImpl.h"

namespace {

using nGZSceneGraph::cViewerRefNodeBaseImpl;

const HookSpec kHooks[] = {
    {"FUN_a0ae06d0", 0xae06d0, 32, 0xbdf961a5, MemberAddress(&cViewerRefNodeBaseImpl::Construct)},
    {"FUN_a0ae08a0", 0xae08a0, 32, 0xcd1cb899, MemberAddress(&cViewerRefNodeBaseImpl::Destruct)},
    {"FUN_a0ae0b90", 0xae0b90, 32, 0x0f40e01c, MemberAddress(&cViewerRefNodeBaseImpl::GetDepthSurface)},
    {"FUN_a0ae0bd0", 0xae0bd0, 32, 0xc404aaab, MemberAddress(&cViewerRefNodeBaseImpl::GetColorSurface)},
    {"FUN_a0ae0c10", 0xae0c10, 11, 0xb028b795, MemberAddress(&cViewerRefNodeBaseImpl::UsesDeviceTargets)},
    {"FUN_a0ae0ce0", 0xae0ce0, 23, 0x8441f39e, MemberAddress(&cViewerRefNodeBaseImpl::BoundSlot70)},
    {"FUN_a0ae0d00", 0xae0d00, 25, 0x61c4ab68, MemberAddress(&cViewerRefNodeBaseImpl::BoundSlot73)},
    {"FUN_a0ae0d20", 0xae0d20, 25, 0xc76b76a3, MemberAddress(&cViewerRefNodeBaseImpl::IntersectsLinear)},
    {"FUN_a0ae0d40", 0xae0d40, 25, 0xdb074a2d, MemberAddress(&cViewerRefNodeBaseImpl::FirstHitLinear)},
    {"FUN_a0ae0d60", 0xae0d60, 32, 0x05f6733f, MemberAddress(&cViewerRefNodeBaseImpl::GetBoundBox)},
    {"FUN_a0ae0db0", 0xae0db0, 32, 0x55bcb09e, MemberAddress(&cViewerRefNodeBaseImpl::GetBoundBoxIn)},
    {"FUN_a0ae0e00", 0xae0e00, 26, 0x3f4ba851, MemberAddress(&cViewerRefNodeBaseImpl::CallTextureSlot7)},
    {"FUN_a0ae0e20", 0xae0e20, 32, 0x841e99a1, MemberAddress(&cViewerRefNodeBaseImpl::Read)},
    {"FUN_a0ae1320", 0xae1320, 21, 0xbe12902b, MemberAddress(&cViewerRefNodeBaseImpl::IsDeviceViewMode)},
    {"FUN_a0ae1470", 0xae1470, 32, 0x7856ec1f, MemberAddress(&cViewerRefNodeBaseImpl::SetSurfaces)},
    {"FUN_a0ae14d0", 0xae14d0, 32, 0xb683df90, MemberAddress(&cViewerRefNodeBaseImpl::SetRefA8)},
    {"FUN_a0ae1510", 0xae1510, 32, 0x8188d25d, MemberAddress(&cViewerRefNodeBaseImpl::SetRefA4)},
    {"FUN_a0ae1550", 0xae1550, 32, 0xee2c4898, MemberAddress(&cViewerRefNodeBaseImpl::SetExplicit)},
    {"FUN_a0ae1600", 0xae1600, 32, 0xf4aee49e, MemberAddress(&cViewerRefNodeBaseImpl::SetTextures)},
    {"FUN_a0ae16e0", 0xae16e0, 32, 0x2dfc05ec, MemberAddress(&cViewerRefNodeBaseImpl::SetTexture)},
    {"FUN_a0ae17a0", 0xae17a0, 32, 0xf44ac379, MemberAddress(&cViewerRefNodeBaseImpl::SetTextureByName)},
    {"FUN_a0ae1870", 0xae1870, 32, 0x52354fac, MemberAddress(&cViewerRefNodeBaseImpl::SetBound)},
    {"FUN_a0ae19b0", 0xae19b0, 32, 0x0c956ff1, MemberAddress(&cViewerRefNodeBaseImpl::SetBoundAndPlane)},
    {"FUN_a0ae19e0", 0xae19e0, 29, 0xa5a83c9d, MemberAddress(&cViewerRefNodeBaseImpl::IsKindOf)},
    {"FUN_a0ae1a00", 0xae1a00, 32, 0xd1d127d2, MemberAddress(&cViewerRefNodeBaseImpl::GetTypeInfo)},
    {"FUN_a0ae1ad0", 0xae1ad0, 32, 0x68224125, MemberAddress(&cViewerRefNodeBaseImpl::Write)},
    {"nGZSceneGraph_cViewerRefNodeBaseImpl_GetTargetSize", 0xae1000, 32, 0x526e21d6, MemberAddress(&cViewerRefNodeBaseImpl::GetTargetSize)},
    {"nGZSceneGraph_cViewerRefNodeBaseImpl_GetTextureRef94", 0xae10f0, 32, 0xa8acdd7d, MemberAddress(&cViewerRefNodeBaseImpl::GetTextureRef94)},
    {"nGZSceneGraph_cViewerRefNodeBaseImpl_GetTextureN", 0xae1200, 32, 0xa8acdd7d, MemberAddress(&cViewerRefNodeBaseImpl::GetTextureN)},
    {"nGZSceneGraph_cViewerRefNodeBaseImpl_GetHandleTexture", 0xae1340, 32, 0xe13c19fe, MemberAddress(&cViewerRefNodeBaseImpl::GetHandleTexture)},
    {"nGZSceneGraph_cViewerRefNodeBaseImpl_CullAndDraw", 0xae09e0, 32, 0xfae9b33f, MemberAddress(&cViewerRefNodeBaseImpl::CullAndDraw)},
    {"nGZSceneGraph_cViewerRefNodeBaseImpl_OnEvent", 0xae0c20, 32, 0xd79128b0, MemberAddress(&cViewerRefNodeBaseImpl::OnEvent)},
};
REGISTER_HOOKS(kHooks);

} // namespace
