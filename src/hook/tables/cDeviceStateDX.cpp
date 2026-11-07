#include "hook/hooks.h"
#include "gz/cDeviceStateDX.h"

namespace {

using nGZGraphic4::cDeviceStateDX;
using nGZGraphic4::cDeviceStateDXAdapters;
namespace devstate = nGZGraphic4::devstate;

// The constructor hook is the class's switch (docs/standalone-shape.md): every state cache the device creates carries
// our vtable, so the virtual slots need no hooks (status "in_effect": "callers", trap
// Hooks.nGZGraphic4_cDeviceStateDX_Construct=1). The non-virtual members are called directly by the device and the
// state commands (still game code), so they are hooked at their RVAs.
const HookSpec kHooks[] = {
    {"nGZGraphic4_cDeviceStateDX_Construct", 0x17c490, 32, 0xb5d43325, reinterpret_cast<void *>(&cDeviceStateDXAdapters::Construct)},
    {"nGZGraphic4_cDeviceStateDX_Flush", 0x17cdf0, 32, 0xa622caef, MemberAddress(&cDeviceStateDX::Flush)},
    {"nGZGraphic4_cDeviceStateDX_MarkTargetsDirty", 0x17ec40, 11, 0xc47b0775, MemberAddress(&cDeviceStateDX::MarkTargetsDirty)},
    {"nGZGraphic4_cDeviceStateDX_SetBackBufferTarget", 0x180050, 32, 0x8947dad6, MemberAddress(&cDeviceStateDX::SetBackBufferTarget)},
    {"nGZGraphic4_cDeviceStateDX_ResetTexCoordStates", 0x17ff90, 32, 0x128888ca, MemberAddress(&cDeviceStateDX::ResetTexCoordStates)},
    {"nGZGraphic4_cDeviceStateDX_RestoreTexCoordStates", 0x17eb90, 32, 0x44eb5b7d, MemberAddress(&cDeviceStateDX::RestoreTexCoordStates)},
    {"nGZGraphic4_cDeviceStateDX_ReleaseDeviceObjects", 0x1815f0, 32, 0x5a5abd45, MemberAddress(&cDeviceStateDX::ReleaseDeviceObjects)},
    {"nGZGraphic4_cDeviceStateDX_SetRenderState", 0x17f8d0, 32, 0xf0b741ac, MemberAddress(&cDeviceStateDX::SetRenderState)},
    {"nGZGraphic4_cDeviceStateDX_SetRenderStates", 0x17f940, 32, 0x876de87d, MemberAddress(&cDeviceStateDX::SetRenderStates)},
    {"nGZGraphic4_cDeviceStateDX_SetTextureStageState", 0x180230, 32, 0xe1ef3e2a, MemberAddress(&cDeviceStateDX::SetTextureStageState)},
    {"nGZGraphic4_cDeviceStateDX_SetTextureStageStates", 0x1802b0, 32, 0xa6a0dc1e, MemberAddress(&cDeviceStateDX::SetTextureStageStates)},
    {"nGZGraphic4_cDeviceStateDX_SetSamplerStates", 0x17fac0, 32, 0xf6c20881, MemberAddress(&cDeviceStateDX::SetSamplerStates)},
    {"nGZGraphic4_cDeviceStateDX_SetTextureTransform", 0x180350, 32, 0xee79b215, MemberAddress(&cDeviceStateDX::SetTextureTransform)},
    {"nGZGraphic4_cDeviceStateDX_SetMaterial", 0x17f0a0, 32, 0x4dd801c3, MemberAddress(&cDeviceStateDX::SetMaterial)},
    {"nGZGraphic4_cDeviceStateDX_SetLight", 0x17efb0, 32, 0xff8ba835, MemberAddress(&cDeviceStateDX::SetLight)},
    {"nGZGraphic4_cDeviceStateDX_SetLightCount", 0x17f6a0, 32, 0xf867f425, MemberAddress(&cDeviceStateDX::SetLightCount)},
    {"nGZGraphic4_cDeviceStateDX_SetClipping", 0x17ec50, 32, 0x0c3c7a64, MemberAddress(&cDeviceStateDX::SetClipping)},
    {"nGZGraphic4_cDeviceStateDX_SetDepthRange", 0x17eca0, 32, 0x55dcd005, MemberAddress(&cDeviceStateDX::SetDepthRange)},
    {"nGZGraphic4_cDeviceStateDX_SetViewportFractions", 0x180720, 32, 0x99fb86cd, MemberAddress(&cDeviceStateDX::SetViewportFractions)},
    {"nGZGraphic4_cDeviceStateDX_SetScissor", 0x17fb60, 32, 0x97ed5166, MemberAddress(&cDeviceStateDX::SetScissor)},
    {"nGZGraphic4_cDeviceStateDX_SetRenderTargets", 0x17f9e0, 32, 0xe3e199df, MemberAddress(&cDeviceStateDX::SetRenderTargets)},
    {"nGZGraphic4_cDeviceStateDX_CopyMatrixTransposed", 0x17d980, 32, 0x4a251d2b, reinterpret_cast<void *>(&cDeviceStateDX::CopyMatrixTransposed)},
    {"nGZGraphic4_DeviceState_SetDefaults", 0x17c1a0, 32, 0x1d877115, MemberAddress(&devstate::DeviceState::SetDefaults)},
    {"nGZGraphic4_StateDelta_Clear", 0x17dee0, 32, 0x776b81b8, MemberAddress(&devstate::StateDelta::Clear)},
    // The state dump (lane RJ; src/gz/cDeviceStateDXDump.cpp); its only caller, the device's dump 0x169f70, is ours.
    {"nGZGraphic4_cDeviceStateDX_DumpInfo", 0x17da90, 32, 0x13ffde83, MemberAddress(&cDeviceStateDX::DumpInfo)},
};
REGISTER_HOOKS(kHooks);

} // namespace
