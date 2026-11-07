#include "hook/hooks.h"
#include "gz/GraphicServices.h"

using namespace nGZGraphic4;

namespace {

// The graphic system's current device (cached) and the pixel format for a colour type (lane OZ).
const HookSpec kHooks[] = {
    {"nGZGraphic4_GetCurrentDeviceCached", 0x18ae40, 32, 0x37beb789, reinterpret_cast<void *>(&GetCurrentDeviceCached)},
    // The current device's enumeration (lane RB).
    {"nGZGraphic4_GetCurrentDeviceEnum", 0x18b470, 19, 0x41f7889f, reinterpret_cast<void *>(&GetCurrentDeviceEnum)},
    {"nGZGraphic4_GetPixelFormatForColorType", 0x18a7f0, 32, 0xa428aab3, reinterpret_cast<void *>(&GetPixelFormatForColorType)},
};
REGISTER_HOOKS(kHooks);

} // namespace
