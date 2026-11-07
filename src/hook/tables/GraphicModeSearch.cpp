#include "hook/hooks.h"
#include "gz/GraphicModeSearch.h"

namespace {

// The graphic system's mode and format searches and the device switch (lane RJ).
const HookSpec kHooks[] = {
    {"nGZGraphic4_FindClosestDisplayMode", 0x18b490, 32, 0xfc355e66, reinterpret_cast<void *>(&nGZGraphic4::FindClosestDisplayMode)},
    {"nGZGraphic4_FindClosestColorFormat", 0x18a9b0, 32, 0x7957c7e2, reinterpret_cast<void *>(&nGZGraphic4::FindClosestColorFormat)},
    {"nGZGraphic4_FindClosestDepthFormat", 0x18ab60, 32, 0x7957c7e2, reinterpret_cast<void *>(&nGZGraphic4::FindClosestDepthFormat)},
    {"nGZGraphic4_FindFormatLowerBound", 0x18ad50, 32, 0x50f60e6f, reinterpret_cast<void *>(&nGZGraphic4::FindFormatLowerBound)},
    {"nGZGraphic4_SetDeviceMode", 0x18b250, 32, 0x83d7fbbe, reinterpret_cast<void *>(&nGZGraphic4::SetDeviceMode)},
    {"nGZGraphic4_SetDeviceDesktopMode", 0x18b440, 32, 0x52019449, reinterpret_cast<void *>(&nGZGraphic4::SetDeviceDesktopMode)},
};
REGISTER_HOOKS(kHooks);

} // namespace
