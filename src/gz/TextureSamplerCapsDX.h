#pragma once
// The sampler state fitting (lane VU, O-c): a texture's filter and mip filter states brought within what its device
// can do, before the scene graph's texture manager (cTextureManager::SamplerStates, src/sg/cTextureManager.cpp) hands
// them to the state cache. One for 2D textures (0x18cc40), one for cube textures (0x18e320), next to the texture
// support checks (gz/TextureSupportDX.h) whose result they start from.
//
// Both, in order:
// - the texture's Direct3D texture (its QueryInterface, not released) checked for filtering; when it can't be, the
//   filter state's two filters 0, its anisotropy 1, the mip filter 0 and the bias 0, and nothing else;
// - the device's caps (the texture's device, its enumeration, cIDeviceEnum's current caps; the enumeration released at
//   the end);
// - the anisotropy clamped to [1, MaxAnisotropy];
// - each filter (+0xc against the D3DPTFILTERCAPS_MAGF bits, +0xd against the MINF ones; 2 anisotropic, 1 linear,
//   0 point) stepped down while the caps lack it: 2 to 1, 1 to 0, and 0 up to 1 when point is missing;
// - a non-zero bias (NaN counts) zeroed without D3DPRASTERCAPS_MIPMAPLODBIAS;
// - the mip filter 0 without mipmapping (D3DPTEXTURECAPS_MIPMAP, the cube's MIPCUBEMAP), then 2 to 1 and 1 to 0 while
//   the MIPF caps lack them;
// - 2D only: with D3DPTEXTURECAPS_POW2, a level-0 width or height that isn't a power of two (0 counts as one) sets the
//   anisotropy 1, the bias 0 and the mip filter 0.
// The filter caps are TextureFilterCaps for 2D and CubeTextureFilterCaps for the cube.
#include "gz/RenderStates.h"

namespace nGZGraphic4 {

class cITextureBase;

void __cdecl FitTextureSamplerStates(cTextureFilterState *filter, cTextureMipFilterState *mip,
                                     cITextureBase *texture); // 0x18cc40
void __cdecl FitCubeTextureSamplerStates(cTextureFilterState *filter, cTextureMipFilterState *mip,
                                         cITextureBase *texture); // 0x18e320

} // namespace nGZGraphic4
