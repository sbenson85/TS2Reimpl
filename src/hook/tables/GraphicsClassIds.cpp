#include "hook/hooks.h"
#include "gz/GraphicsClassIds.h"

namespace {

const HookSpec kHooks[] = {
    {"gfxclassid_cGZGraphicSystemId", 0x147760, 6, 0x2f6d29d3, &gfxclassid::cGZGraphicSystemId},
    {"gfxclassid_cGZCanvasId", 0x1496b0, 6, 0x4bbee73c, &gfxclassid::cGZCanvasId},
    {"gfxclassid_cDraw2DSystemServiceId", 0x14f320, 6, 0xd879aa0a, &gfxclassid::cDraw2DSystemServiceId},
    {"gfxclassid_Class_dc012582Id", 0x153a10, 6, 0x35b3ff6b, &gfxclassid::Class_dc012582Id},
    {"gfxclassid_Class_9c1d3c77Id", 0x153a20, 6, 0x7d332d42, &gfxclassid::Class_9c1d3c77Id},
    {"gfxclassid_Class_5c0ac4afId", 0x153a30, 6, 0x55f74529, &gfxclassid::Class_5c0ac4afId},
    {"gfxclassid_cCanvasW32Id", 0x155e10, 6, 0xf7068c9e, &gfxclassid::cCanvasW32Id},
    {"gfxclassid_cGraphicSystemDXId", 0x159a20, 6, 0x409a8e8c, &gfxclassid::cGraphicSystemDXId},
    {"gfxclassid_cDisplayMonitorW32Id", 0x154610, 6, 0x6dd1d0f5, &gfxclassid::cDisplayMonitorW32Id},
    {"gfxclassid_cPixelFormatIteratorId", 0x15eb00, 6, 0x67f184f1, &gfxclassid::cPixelFormatIteratorId},
    {"gfxclassid_cImageSurfaceDXId", 0x161e70, 6, 0xbc2b3682, &gfxclassid::cImageSurfaceDXId},
    {"gfxclassid_cDepthStencilSurfaceDXId", 0x161e60, 6, 0xb0f37f33, &gfxclassid::cDepthStencilSurfaceDXId},
    {"gfxclassid_cRenderTargetDXId", 0x161e90, 6, 0x44d5e86d, &gfxclassid::cRenderTargetDXId},
    {"gfxclassid_cLockableRenderTargetDXId", 0x161e80, 6, 0x2c0cac4e, &gfxclassid::cLockableRenderTargetDXId},
    {"gfxclassid_cTextureDXId", 0x1636b0, 6, 0x551b5047, &gfxclassid::cTextureDXId},
    {"gfxclassid_cCubeTextureDXId", 0x164950, 6, 0x9be1ddc9, &gfxclassid::cCubeTextureDXId},
    {"gfxclassid_cStaticVertexComponentArrayDXId", 0x166080, 6, 0x19661a4f, &gfxclassid::cStaticVertexComponentArrayDXId},
    {"gfxclassid_cDynamicVertexComponentArrayDXId", 0x166060, 6, 0xd01b019f, &gfxclassid::cDynamicVertexComponentArrayDXId},
    {"gfxclassid_cStaticVertexIndexArrayDXId", 0x166090, 6, 0x8d190d54, &gfxclassid::cStaticVertexIndexArrayDXId},
    {"gfxclassid_cDynamicVertexIndexArrayDXId", 0x166070, 6, 0x1874c225, &gfxclassid::cDynamicVertexIndexArrayDXId},
    {"gfxclassid_cGZDrawContextId", 0x8bf910, 6, 0xf7a46962, &gfxclassid::cGZDrawContextId},
    {"gfxclassid_cGZDrawSystemId", 0x8c09f0, 6, 0x5f5f9ca5, &gfxclassid::cGZDrawSystemId},
    {"gfxclassid_cGZDrawContextAsBufferId", 0x8c2340, 6, 0x3a6e8b06, &gfxclassid::cGZDrawContextAsBufferId},
    {"gfxclassid_cGZDrawFontRendererId", 0x8c33b0, 6, 0x03591e73, &gfxclassid::cGZDrawFontRendererId},
    {"gfxclassid_cGZBufferId", 0x8c7610, 6, 0x83916fa8, &gfxclassid::cGZBufferId},
    {"gfxclassid_cGZTransientBufferId", 0x8c7690, 6, 0xbd179d3a, &gfxclassid::cGZTransientBufferId},
};

REGISTER_HOOKS(kHooks);

} // namespace
