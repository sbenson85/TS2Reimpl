#pragma once
// nGZSceneGraph::ChooseTextureDescription (lane TH, milestone O row M4b): the device texture description the scene
// graph's textures create from. cTextureImage::Restore (0xab5610) and cProceduralTexture::Restore (0xab5090) call it;
// cRenderTargetTexture chooses its own.
#include <cstdint>

namespace nGZGraphic4 {
class cIPixelFormat;
class cIDeviceDX;
}

namespace nGZSceneGraph {

// The description {width, height, levels, format} CreateDeviceTexture (0xab57d0) takes.
struct TextureDescription {
    int32_t width;
    int32_t height;
    uint32_t levels;
    nGZGraphic4::cIPixelFormat *format; // not counted
};
static_assert(sizeof(TextureDescription) == 0x10, "TextureDescription: four words");

// 0xab5f50 (cdecl, hooked). `desc` is zeroed, then the format is chosen for a texture of `kind` (4 a 2D texture, 6 a
// cube; the device enumeration's usage index) on the current device:
//  - the image's format (normalMap: X8R8G8B8 from the formats manager instead) when the device takes it and the
//    texture manager's filter flags allow it (bit 0: more than 16 bits per pixel; bit 1: DXT);
//  - else a DXT image's own DXT format when bit 1 allows it and the device takes it;
//  - else the closest colour format the device has for the image's bits (DXT: 24 or 12 colour bits by bit 0, alpha 1
//    for DXT1 else 8; otherwise the ARGB face's channels, the colour sum held to 16 unless bit 0), then at half both.
// The size and levels are then fitted to the device: AdjustTextureSize (0x18c990) for kind 4; for kind 6 the edge is
// the mean of width and height, fitted by 0x18e1d0. False (desc left zero) when no format is found.
// ORIGINAL-BUG: no null checks on the device, on the ARGB face of a non-DXT format, or on the faces of the format the
// device was asked about.
bool __cdecl ChooseTextureDescription(TextureDescription *desc, int32_t width, int32_t height, uint32_t levels,
                                      nGZGraphic4::cIPixelFormat *format, bool normalMap, uint32_t kind);

// 0x18c990 (cdecl, ours, hooked: the game's cRenderTargetTexture, cPatternBrush and others call it too; ours from
// ChooseTextureDescription and cDraw2D): fits a 2D texture's size and levels to the device's caps for the format. Each
// side at least 1; unless `allowNonPow2` and the device takes conditional non-powers of two (and the format isn't DXT),
// with power-of-two caps each side goes to the next power of two, or (allowed, and both sides above 32) to the one at or
// below it. Then a multiple of 4 each for DXT, an even width for YUV, the caps' maxima, the aspect ratio (square only:
// 1; else the caps' maximum, none when 0) raising the shorter side. The levels: 1 for any size or without mipmaps, else
// held between 0 and the halvings until both sides are 1. Not inlined, so tests can stand it in.
__declspec(noinline) void __cdecl AdjustTextureSize(nGZGraphic4::cIDeviceDX *device, nGZGraphic4::cIPixelFormat *format,
                                                     int32_t *width, int32_t *height, uint32_t *levels,
                                                     bool allowNonPow2);

// 0x18e1d0 (cdecl, ours; called only by ChooseTextureDescription): fits a cube texture's edge and levels to the device's
// caps for the format. The edge: at least 1, the next power of two when the caps ask for it, a multiple of 4 for DXT and
// of 2 for YUV, at most the smaller of the caps' maximum width and height. The levels: 1 without mip cube maps, else
// held between 0 and the halvings the edge allows. Not inlined, so tests can stand it in.
__declspec(noinline) void __cdecl AdjustCubeTextureSize(nGZGraphic4::cIDeviceDX *device,
                                                         nGZGraphic4::cIPixelFormat *format, int32_t *edge,
                                                         uint32_t *levels);

} // namespace nGZSceneGraph
