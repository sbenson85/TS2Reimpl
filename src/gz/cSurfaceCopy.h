#pragma once
// The surface copies of nGZGraphic4 (lane OS, rendering R3b step 4): the clip, the StretchRect check, the locked row
// copy, the system-memory twin and the copy that picks between them by the surfaces' pools. All cdecl free functions
// of the game's graphics layer; the surfaces' slots (cSurfaceDX.h: CopyRect, Copy, StretchRect) and the image
// manipulator, the 2D drawing and the device enumeration call them. docs/subsystems/rendering.md, R3b.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d9.h>

#include <cstdint>

namespace nGZGraphic4 {

class cISurfaceDX;

// 0x18ce70: cuts `sourceRect` to `sourceBounds` and `targetRect` to `targetBounds`, the target cut in the proportion of
// the source's cut (in floats), and the target rectangle by its bounds again, the source cut back in the same
// proportion; false when nothing (at least a pixel each way) is left. Rectangles are left, top, right, bottom words.
bool ClipCopyRects(RECT *sourceRect, const RECT *sourceBounds, RECT *targetRect, const RECT *targetBounds);

// 0x162d60: whether IDirect3DDevice9::StretchRect can copy `sourceRect` of `source` to `targetRect` of `target` (both in
// the default pool, different surfaces, not both depth stencil and render target kinds at once, and the device
// enumeration agrees). On true `*filter` is 0 or, when the caps allow, 1 (point) or, with `wantFilter` (the sizes
// differ), 2 (linear). The descriptions are the surfaces' Direct3D descriptions.
bool CanStretchRectDX(cISurfaceDX *source, const D3DSURFACE_DESC *sourceDesc, const RECT *sourceRect,
                      cISurfaceDX *target, const D3DSURFACE_DESC *targetDesc, const RECT *targetRect, uint32_t *filter,
                      bool wantFilter);

// 0x160ef0: locks both surfaces and copies the rows of `sourceRect` to `targetRect`'s lock (as many bytes a row as
// the source rectangle's width takes in the source's format). False when either surface sits in a default-pool
// texture that isn't dynamic, or a lock fails.
bool CopySurfaceRectLocked(IDirect3DSurface9 *source, const RECT *sourceRect, IDirect3DSurface9 *target,
                           const RECT *targetRect);

// 0x161b20: makes a system-memory surface of the size and format of `source`: of its container's kind when it has one
// (a texture's level 0 into `*texture`, a cube texture's face 0 into `*cube`), else a plain offscreen surface; the
// surface (counted) is `*surface`. The caller releases whatever was made.
bool MakeSystemMemorySurfaceLike(IDirect3DSurface9 **surface, IDirect3DTexture9 **texture,
                                 IDirect3DCubeTexture9 **cube, IDirect3DSurface9 *source);

// 0x1614b0: copies `sourceRect` of `source` to `targetPoint` of `target`, clipped to both, by the cheapest way the
// surfaces' usage and pools allow (GetRenderTargetData, StretchRect, UpdateSurface, a locked copy, or through a
// system-memory twin). True when it copied or when nothing was left to copy.
bool CopySurfaceRectDX(cISurfaceDX *source, const RECT *sourceRect, cISurfaceDX *target, const POINT *targetPoint);

} // namespace nGZGraphic4
