#include "ts/TSBehaviorCopies.h"

namespace nTS::behavior_copies {

Map *__cdecl CameraControllerManagerRegistryFor(uint32_t id, const Attribute *attribute)
{
    return OriginalFunction<Map *(__cdecl *)(uint32_t, const Attribute *), 0xcc3550>()(id, attribute);
}

Map *__cdecl ConfigurationChangeManagerRegistryFor(uint32_t id, const Attribute *attribute)
{
    return OriginalFunction<Map *(__cdecl *)(uint32_t, const Attribute *), 0xcd6c00>()(id, attribute);
}

Map *__cdecl SmoothFirstPersonRegistryFor(uint32_t id, const Attribute *attribute)
{
    return OriginalFunction<Map *(__cdecl *)(uint32_t, const Attribute *), 0xb4f2a0>()(id, attribute);
}

Map *__cdecl ConfigDeviceSettingsRegistryFor(uint32_t id, const Attribute *attribute)
{
    return OriginalFunction<Map *(__cdecl *)(uint32_t, const Attribute *), 0xb6f910>()(id, attribute);
}

Map *__cdecl MainToolbarRegistry() { return OriginalFunction<Map *(__cdecl *)(), 0x61fea4>()(); }

Map *__cdecl ScreenGrabberRegistry() { return OriginalFunction<Map *(__cdecl *)(), 0x60e453>()(); }

} // namespace nTS::behavior_copies
