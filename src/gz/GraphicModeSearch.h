#pragma once
// The graphic system's mode and format searches and the device switch (lane RJ, milestone O row O-c2; 0x18ad50 lane VT): six cdecl
// helpers of GraphicServices.cpp's module that cGZGraphicSystem::Init (0x147d10) and cDeviceDX::CreateDevice (0x167ec0)
// call. Each search walks a range the enumeration handed out (gz/cDeviceEnum.h) and releases it before returning
// (end, begin, then the cursor), as the original's callers expect: the range is passed by value and owned by the
// callee. The searches ask the candidates' faces again for every comparison (the original's call pattern, kept).
#include <cstdint>

#include "game/owned.h"
#include "gz/cDeviceEnum.h"

// The "desktop mode" request 0x18b440 passes (all zero in the image; nothing writes it). 0x18b250 compares a request
// with it to decide whether the window must move to the target device's monitor.
TS2_OWNED_STATIC(nGZGraphic4::cDisplayMode, 10ca868);

namespace nGZGraphic4 {

// 0x18b490: the smallest listed mode (by area, then width; then the most bits per pixel, the fewest unused bits and
// the lowest refresh) at least minWidth x minHeight, with minBits..maxBits bits per pixel (signed) and
// minRefresh..maxRefresh Hz. Writes it to `mode` and answers true when one fits; `mode` is left alone otherwise.
// Each mode's format is asked for its cIPixelFormatARGB face (0x9ac38991) unchecked, as in the original.
bool __cdecl FindClosestDisplayMode(cDisplayMode *mode, cDisplayModeRange modes, uint32_t minWidth, uint32_t minHeight,
                                    int32_t minBits, int32_t maxBits, uint32_t minRefresh, uint32_t maxRefresh);

// 0x18a9b0: the ARGB format of `formats` with at least minBits bits and minAlphaBits alpha bits that has the fewest
// bits; ties go to the fewest alpha bits, or with minAlphaBits non-zero to the fewest unused bits, then the fewest alpha
// bits. Formats without an ARGB face are skipped. Null when none fits; the answer is not counted.
cIPixelFormat *__cdecl FindClosestColorFormat(cPixelFormatRange formats, int32_t minBits, int32_t minAlphaBits);

// 0x18ab60: the depth-stencil format (face 0x5ac389b6) of `formats` with at least minDepthBits depth and
// minStencilBits stencil bits (and a float depth when floatDepth): a float depth beats a fixed one, then the most depth
// bits, then the fewest stencil bits. Null when none fits; not counted.
cIPixelFormat *__cdecl FindClosestDepthFormat(cPixelFormatRange formats, int32_t minDepthBits, int32_t minStencilBits,
                                              bool floatDepth);

// 0x18ad50 (lane VT): the first format of the sorted `formats` that is `wanted` or after it in the formats' order
// (wanted's slot 10, Less(format, false)), as std::lower_bound. ORIGINAL-BUG: when every format is before `wanted`, the
// last one is answered; null only for an empty range. Not counted. Callers: cRenderTargetTexture::Restore (0xab5240,
// the float target's format) and cPatternBrush slot 3 (0x153ae0).
cIPixelFormat *__cdecl FindFormatLowerBound(cPixelFormatRange formats, cIPixelFormat *wanted);

// 0x18b250: device `device` (-1: the current one) made current with these settings (cDeviceSettings' words). The old
// current device is released first when it is another one; with the desktop mode request (ts2_rva_10ca868) and a
// device other than the window's, the window is placed on the target's monitor. When the target refuses the settings,
// it is released, the old device becomes current again and gets its old settings back if it had a device (slot 24
// when that fails too). False without a current device, for an index past the count, or when the target refused.
bool __cdecl SetDeviceMode(int32_t device, const cDisplayMode *mode, cIPixelFormat *colorFormat, cIPixelFormat *depthFormat,
                           uint32_t backBufferCount, uint32_t multisampleType, uint32_t multisampleQuality, uint32_t flags);

// 0x18b440: SetDeviceMode with the desktop mode request.
bool __cdecl SetDeviceDesktopMode(int32_t device, cIPixelFormat *colorFormat, cIPixelFormat *depthFormat,
                                  uint32_t backBufferCount, uint32_t multisampleType, uint32_t multisampleQuality,
                                  uint32_t flags);

} // namespace nGZGraphic4
