#pragma once
// The graphic system's cached manager getters (lane OW, rendering): five cdecl functions, each the same
// lazily cached lookup. The framework's service object (GetServiceObject) is asked through its slot 5 for the graphic
// system (ids 0x85522803, 0xa55227bb; no null check on the answer, as in the original); the function then calls one of
// the graphic system's getters (cIGraphicSystemDX), keeps the answer in its own global and lets the temporary holder go
// (ReleaseHeldObject, 0x1466e0). Every later call returns the cached global without asking again.
//
// The globals are not reset anywhere: the managers are static objects of the graphic system, so the answer does not
// change for the life of the process.
#include <cstdint>

#include "game/owned.h"

// The getters' caches: the current device's graphic system, then the pixel formats, image manip, vertex formats,
// vertex declarations and shader programs managers.
TS2_OWNED_STATIC(void *, 10ca87c);
TS2_OWNED_STATIC(void *, 10ca830);
TS2_OWNED_STATIC(void *, 10ca8fc);
TS2_OWNED_STATIC(void *, 10ca990);
TS2_OWNED_STATIC(void *, 10caa24);
TS2_OWNED_STATIC(void *, 10caa28);
TS2_OWNED_STATIC(void *, 10caa3c);

namespace nGZGraphic4 {

class cIDeviceDX;

void *__cdecl GetPixelFormatsManagerCached();       // 0x18acd0: cIGraphicSystemDX slot 16, global 0x10ca830
void *__cdecl GetImageManipCached();                // 0x18d120: slot 20, global 0x10ca990
void *__cdecl GetVertexFormatsManagerCached();      // 0x18dcf0: slot 17, global 0x10caa24
void *__cdecl GetVertexDeclarationsManagerCached(); // 0x18dd70: slot 18, global 0x10caa28
void *__cdecl GetShaderProgramsManagerCached();     // 0x18ddf0: slot 19, global 0x10caa3c

// Lane WN. 0x18c000: the Draw2D service's renderer (the service 0x89027355 asked for its renderer, slot 13), cached at
// 0x10ca8fc, not counted.
void *__cdecl GetDraw2DCached();

// Lane OZ. 0x18ae40: the current device. Unlike the getters above, the cached global (0x10ca87c) is the graphic system
// itself (cIGraphicSystemDX, asked for the same way); every call then asks it for its current device index (slot 21) and
// answers the device of that index (slot 24), or null (eax = 0) when the index is negative. The device is not AddRef'd.
cIDeviceDX *__cdecl GetCurrentDeviceCached();

// Lane RB. 0x18b470: the current device's enumeration (its slot 16, GetDeviceEnumConst; not counted), or null without a
// current device.
void *__cdecl GetCurrentDeviceEnum();

// 0x18a7f0: the pixel format object (the formats manager's GetARGB, slot 6, via the cached manager) for a colour type:
// 2 8-bit alpha (8, 8, 0, 0, 0), 4 R5G5B5 (16, 0, 5, 5, 5), 5 R5G6B5 (16, 0, 5, 6, 5), 6 A1R5G5B5 (16, 1, 5, 5, 5),
// 7 A4R4G4B4 (16, 4, 4, 4, 4), 8 X8R8G8B8 (32, 0, 8, 8, 8), 9 A8R8G8B8 (32, 8, 8, 8, 8); any other value: null (not asked).
void *__cdecl GetPixelFormatForColorType(uint32_t colorType);

} // namespace nGZGraphic4
