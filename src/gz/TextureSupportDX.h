#pragma once
// The texture support checks (lane RJ, O-c2): whether a texture's format can be filtered on its device, asked through
// IDirect3D9::CheckDeviceFormat with D3DUSAGE_QUERY_FILTER. RZGraphic4TextureSupportDX.cpp and
// RZGraphic4CubeTextureSupportDX.cpp in the original; used by the state cache's sampler validators
// (src/gz/cDeviceStateDXValidate.cpp) and game code.
#include <d3d9.h>

#include "game/owned.h"

TS2_OWNED_STATIC(const char, da0e78)[]; // the texture check's source file, for its LogD3DError reports
TS2_OWNED_STATIC(const char, da10d8)[]; // the cube texture check's

namespace nGZGraphic4 {

// 0x18c7f0 (cdecl): level 0's format of `texture` checked for filtering on its device's adapter and device type, in the
// display mode's format, with level 0's usage plus D3DUSAGE_QUERY_FILTER and the texture's resource type. Each failing
// call is reported (LogD3DError, the file and its line) and the check goes on; the check's own result is reported when
// it is a failure other than D3DERR_NOTAVAILABLE. False only for D3DERR_NOTAVAILABLE. The device and the IDirect3D9 it
// takes are released.
bool __cdecl TextureFilterSupported(IDirect3DTexture9 *texture);

// 0x18df30 (cdecl): the same for a cube texture (its face 0's level 0).
bool __cdecl CubeTextureFilterSupported(IDirect3DCubeTexture9 *texture);

} // namespace nGZGraphic4
