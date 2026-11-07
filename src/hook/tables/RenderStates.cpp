#include "gz/RenderStates.h"
#include "hook/hooks.h"

using namespace nGZGraphic4;

namespace {

const HookSpec kHooks[] = {
    // Slot 1, CloneInto.
    {"nGZGraphic4_cAlphaBlendState_vf1", 0x14e880, 32, 0xd42b937d, MemberAddress(&cAlphaBlendState::CloneInto)},
    {"nGZGraphic4_cAlphaTestFunction_vf1", 0x14e8c0, 32, 0xbeda270f, MemberAddress(&cAlphaTestFunction::CloneInto)},
    {"nGZGraphic4_cAlphaTestState_vf1", 0x14e8f0, 32, 0x7b2aca0e, MemberAddress(&cAlphaTestState::CloneInto)},
    {"nGZGraphic4_cClippingState_vf1", 0x14e920, 32, 0x713be9fe, MemberAddress(&cClippingState::CloneInto)},
    {"nGZGraphic4_cColorScalarState_vf1", 0x14e950, 32, 0xbe400b51, MemberAddress(&cColorScalarState::CloneInto)},
    {"nGZGraphic4_cColorWriteEnableState_vf1", 0x186a10, 32, 0x336122b0, MemberAddress(&cColorWriteEnableState::CloneInto)},
    {"nGZGraphic4_cCullModeState_vf1", 0x14e980, 32, 0x750a2bfb, MemberAddress(&cCullModeState::CloneInto)},
    {"nGZGraphic4_cDepthTestFunction_vf1", 0x1884c0, 32, 0xe814937f, MemberAddress(&cDepthTestFunction::CloneInto)},
    {"nGZGraphic4_cDepthTestState_vf1", 0x14e9e0, 32, 0x49282635, MemberAddress(&cDepthTestState::CloneInto)},
    {"nGZGraphic4_cFFLightingState_vf1", 0x187180, 32, 0xb40284c2, MemberAddress(&cFFLightingState::CloneInto)},
    {"nGZGraphic4_cFillModeState_vf1", 0x186a40, 32, 0x36df8b96, MemberAddress(&cFillModeState::CloneInto)},
    {"nGZGraphic4_cShadingState_vf1", 0x186a80, 32, 0xa3bfbad4, MemberAddress(&cShadingState::CloneInto)},
    {"nGZGraphic4_cSpecularAddState_vf1", 0x188bd0, 32, 0x421d5bf4, MemberAddress(&cSpecularAddState::CloneInto)},
    {"nGZGraphic4_cStencilOperationState_vf1", 0x188700, 32, 0x61e1ec26, MemberAddress(&cStencilOperationState::CloneInto)},
    {"nGZGraphic4_cTexCoordInterpolationState_vf1", 0x189170, 32, 0x53734802, MemberAddress(&cTexCoordInterpolationState::CloneInto)},
    {"nGZGraphic4_cDepthRangeState_vf1", 0x14e9b0, 32, 0x0021eaf5, MemberAddress(&cDepthRangeState::CloneInto)},
    {"nGZGraphic4_cFFPerVertexColorState_vf1", 0x187210, 32, 0x4dd0640d, MemberAddress(&cFFPerVertexColorState::CloneInto)},
    {"nGZGraphic4_cFFTextureCoordsSourceState_vf1", 0x187260, 32, 0x2d0c61ea, MemberAddress(&cFFTextureCoordsSourceState::CloneInto)},
    {"nGZGraphic4_cFFTextureTransformTypeState_vf1", 0x1872f0, 32, 0xe02b227e, MemberAddress(&cFFTextureTransformTypeState::CloneInto)},
    {"nGZGraphic4_cStencilState_vf1", 0x188740, 32, 0xc8a3d0aa, MemberAddress(&cStencilState::CloneInto)},
    {"nGZGraphic4_cTextureAddressingState_vf1", 0x14e7a0, 32, 0x061fd16a, MemberAddress(&cTextureAddressingState::CloneInto)},
    {"nGZGraphic4_cStencilFunctionState_vf1", 0x1886c0, 32, 0xc4f196a5, MemberAddress(&cStencilFunctionState::CloneInto)},
    {"nGZGraphic4_cTextureFilterState_vf1", 0x14e7f0, 32, 0x061fd16a, MemberAddress(&cTextureFilterState::CloneInto)},
    {"nGZGraphic4_cTextureMipFilterState_vf1", 0x14e840, 32, 0x061fd16a, MemberAddress(&cTextureMipFilterState::CloneInto)},
    {"nGZGraphic4_cFFEnabledLights_vf1", 0x187130, 32, 0x5fe56a52, MemberAddress(&cFFEnabledLights::CloneInto)},
    {"nGZGraphic4_cTextureBlendState_vf1", 0x14ea60, 13, 0x6694bf88, MemberAddress(&cTextureBlendState::CloneInto)},
    {"nGZGraphic4_cRenderTargetsState_vf1", 0x1864b0, 32, 0x5ad0e04a, MemberAddress(&cRenderTargetsState::CloneInto)},
    {"nGZGraphic4_cScissorTestState_vf1", 0x1864e0, 32, 0x85ee4d51, MemberAddress(&cScissorTestState::CloneInto)},
    {"nGZGraphic4_cViewportState_vf1", 0x14ea10, 32, 0x77e32a8e, MemberAddress(&cViewportState::CloneInto)},
    {"nGZGraphic4_cFogState_vf1", 0x186a70, 13, 0xd4657d12, MemberAddress(&cFogState::CloneInto)},
    {"nGZGraphic4_cFFMaterial_vf1", 0x1871c0, 32, 0x8cd519a8, MemberAddress(&cFFMaterial::CloneInto)},
    {"nGZGraphic4_cFFTextureMatrixState_vf1", 0x1872a0, 32, 0x2d0c61ea, MemberAddress(&cFFTextureMatrixState::CloneInto)},
    {"nGZGraphic4_cFFLight_vf1", 0x187170, 13, 0x352f9372, MemberAddress(&cFFLight::CloneInto)},
    // Out-of-line copy constructors of the three large states.
    {"cTextureBlendState_CopyFrom", 0x14bd50, 32, 0xbe19a467, MemberAddress(&cTextureBlendState::CopyFrom)},
    {"cFogState_CopyFrom", 0x186820, 32, 0x7f9a50bb, MemberAddress(&cFogState::CopyFrom)},
    {"cFFLight_CopyFrom", 0x187030, 32, 0x359c0d94, MemberAddress(&cFFLight::CopyFrom)},
    // Slot 0, the deleting destructors: one per state size. The folded bodies (shared_vf_*) are referenced only by the
    // render states' vtables (tools/xrefs.py), so they are hooked as one family's code (docs/verification.md).
    {"nGZGraphic4_cStateHeader_vf0", 0x14c870, 32, 0xea22e0b9, MemberAddress(&cStateHeader::DeletingDestructor<cStateHeader>)},
    {"RenderState_vf0_size_0c", 0x14c620, 32, 0x1c78bdef, MemberAddress(&cStateHeader::DeletingDestructor<cAlphaBlendState>)},
    {"RenderState_vf0_size_10", 0x14c650, 32, 0x2194d148, MemberAddress(&cStateHeader::DeletingDestructor<cDepthRangeState>)},
    {"RenderState_vf0_size_14", 0x14c8d0, 32, 0x9343620a, MemberAddress(&cStateHeader::DeletingDestructor<cStencilFunctionState>)},
    {"RenderState_vf0_size_18", 0x14c8a0, 32, 0xeacd91e2, MemberAddress(&cStateHeader::DeletingDestructor<cTextureBlendState>)},
    {"RenderState_vf0_size_1c", 0x186480, 32, 0xf2c23480, MemberAddress(&cStateHeader::DeletingDestructor<cRenderTargetsState>)},
    {"nGZGraphic4_cViewportState_vf0", 0x14c900, 32, 0xc1b190bc, MemberAddress(&cStateHeader::DeletingDestructor<cViewportState>)},
    {"nGZGraphic4_cFogState_vf0", 0x186890, 32, 0x97116e19, MemberAddress(&cStateHeader::DeletingDestructor<cFogState>)},
    {"RenderState_vf0_size_4c", 0x187100, 32, 0x842f87cc, MemberAddress(&cStateHeader::DeletingDestructor<cFFMaterial>)},
    {"nGZGraphic4_cFFLight_vf0", 0x1870d0, 32, 0x5e184be4, MemberAddress(&cStateHeader::DeletingDestructor<cFFLight>)},
};
REGISTER_HOOKS(kHooks);

} // namespace
