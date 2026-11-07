#pragma once
// The current device's render-target size (lane VU, milestone O row O-c; one of the cdecl device helpers of
// RZGraphic4Services next to GetCurrentDeviceCached 0x18ae40). Used by cRenderTargetTexture::Restore (relative sizes),
// cViewerRefNodeBaseImpl::GetTargetSize and 0xcd6320.
#include <cstdint>

namespace nGZGraphic4 {

class cIPixelFormat;

// What 0x18aea0 writes: the size, the aspect and the format of what the device draws into.
struct cDeviceDisplaySize {
    uint32_t mWidth;        // +0x0
    uint32_t mHeight;       // +0x4
    float mAspect;          // +0x8: the adapter display mode's, kept when the back buffer answers
    cIPixelFormat *mFormat; // +0xc
};
static_assert(sizeof(cDeviceDisplaySize) == 0x10, "cDeviceDisplaySize: 0x10 bytes");

// 0x18aea0 (cdecl; returns `out`): the current device's enumeration's adapter display mode (cIDeviceEnum slot 6:
// width, height, aspect, format); when the device is ready (cIDeviceDX slot 35), the width, height and format of its
// back buffer 0 (slot 39) in place of the mode's. Nothing is counted or released. The device must exist.
cDeviceDisplaySize *__cdecl GetDeviceTargetSize(cDeviceDisplaySize *out);

} // namespace nGZGraphic4
