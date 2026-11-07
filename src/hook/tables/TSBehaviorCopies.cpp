#include "hook/hooks.h"
#include "ts/TSBehaviorCopies.h"

namespace {

using namespace nTS::behavior_copies;

// The game's TS-side copies of cAbstractBehaviorTypeT<I, cRefSingleThreaded> (lane FD; src/ts/TSBehaviorCopies.h): every
// copy's Release, OnEvent, NotifyListeners, AskListeners, Connect, Disconnect, GetEventCount, GetEventAttribute and
// GetEventManager, each hooked to cTSBehaviorCopy's adapter for its copy (the win scene graph's GetEventCount is folded
// with the base registry's other users and stays out).
const HookSpec kHooks[] = {
    {"CameraControllerManagerBehavior_Release", 0xcc3630, 32, 0x2a6dd1db, MemberAddress(&CameraControllerManagerCopy::HookRelease)},
    {"CameraControllerManagerBehavior_OnEvent", 0xcc30b0, 32, 0x1063a69a, MemberAddress(&CameraControllerManagerCopy::HookOnEvent)},
    {"CameraControllerManagerBehavior_NotifyListeners", 0xcc3850, 32, 0x098a7d26, MemberAddress(&CameraControllerManagerCopy::HookNotifyListeners)},
    {"CameraControllerManagerBehavior_AskListeners", 0xcc2f20, 32, 0x6a2520c5, MemberAddress(&CameraControllerManagerCopy::HookAskListeners)},
    {"CameraControllerManagerBehavior_Connect", 0xcc2d50, 32, 0x5f8b04f0, MemberAddress(&CameraControllerManagerCopy::HookConnect)},
    {"CameraControllerManagerBehavior_Disconnect", 0xcc2e90, 32, 0xcfde84ef, MemberAddress(&CameraControllerManagerCopy::HookDisconnect)},
    {"CameraControllerManagerBehavior_GetEventCount", 0xcc34c0, 32, 0x8ff279cf, MemberAddress(&CameraControllerManagerCopy::HookGetEventCount)},
    {"CameraControllerManagerBehavior_GetEventAttribute", 0xcc2c70, 32, 0x44150bbb, MemberAddress(&CameraControllerManagerCopy::HookGetEventAttribute)},
    {"CameraControllerManagerBehavior_GetEventManager", 0xcc2d70, 32, 0x697563d9, reinterpret_cast<void *>(&CameraControllerManagerCopy::HookGetEventManager)},
    {"ConfigurationChangeManagerBehavior_Release", 0xcd7160, 32, 0x78328b39, MemberAddress(&ConfigurationChangeManagerCopy::HookRelease)},
    {"ConfigurationChangeManagerBehavior_OnEvent", 0xcd6940, 32, 0xe2957fe5, MemberAddress(&ConfigurationChangeManagerCopy::HookOnEvent)},
    {"ConfigurationChangeManagerBehavior_NotifyListeners", 0xcd7240, 32, 0x098a7d26, MemberAddress(&ConfigurationChangeManagerCopy::HookNotifyListeners)},
    {"ConfigurationChangeManagerBehavior_AskListeners", 0xcd6180, 32, 0x6a2520c5, MemberAddress(&ConfigurationChangeManagerCopy::HookAskListeners)},
    {"ConfigurationChangeManagerBehavior_Connect", 0xcd5da0, 32, 0x5f8b04f0, MemberAddress(&ConfigurationChangeManagerCopy::HookConnect)},
    {"ConfigurationChangeManagerBehavior_Disconnect", 0xcd5e40, 32, 0x4d13b28e, MemberAddress(&ConfigurationChangeManagerCopy::HookDisconnect)},
    {"ConfigurationChangeManagerBehavior_GetEventCount", 0xcd6b40, 32, 0x8ff279cf, MemberAddress(&ConfigurationChangeManagerCopy::HookGetEventCount)},
    {"ConfigurationChangeManagerBehavior_GetEventAttribute", 0xcd5cc0, 32, 0x44150bbb, MemberAddress(&ConfigurationChangeManagerCopy::HookGetEventAttribute)},
    {"ConfigurationChangeManagerBehavior_GetEventManager", 0xcd5dc0, 32, 0x882472af, reinterpret_cast<void *>(&ConfigurationChangeManagerCopy::HookGetEventManager)},
    {"SmoothFirstPersonBehavior_Release", 0xb4f380, 32, 0x3fec5bab, MemberAddress(&SmoothFirstPersonCopy::HookRelease)},
    {"SmoothFirstPersonBehavior_OnEvent", 0xb4ec50, 32, 0x0ab41028, MemberAddress(&SmoothFirstPersonCopy::HookOnEvent)},
    {"SmoothFirstPersonBehavior_NotifyListeners", 0xb4f870, 32, 0x098a7d26, MemberAddress(&SmoothFirstPersonCopy::HookNotifyListeners)},
    {"SmoothFirstPersonBehavior_AskListeners", 0xb4eb20, 32, 0x6a2520c5, MemberAddress(&SmoothFirstPersonCopy::HookAskListeners)},
    {"SmoothFirstPersonBehavior_Connect", 0xb4e7c0, 32, 0x5f8b04f0, MemberAddress(&SmoothFirstPersonCopy::HookConnect)},
    {"SmoothFirstPersonBehavior_Disconnect", 0xb4e870, 32, 0x564c635e, MemberAddress(&SmoothFirstPersonCopy::HookDisconnect)},
    {"SmoothFirstPersonBehavior_GetEventCount", 0xb4f1d0, 32, 0x8ff279cf, MemberAddress(&SmoothFirstPersonCopy::HookGetEventCount)},
    {"SmoothFirstPersonBehavior_GetEventAttribute", 0xb4e6c0, 32, 0x44150bbb, MemberAddress(&SmoothFirstPersonCopy::HookGetEventAttribute)},
    {"SmoothFirstPersonBehavior_GetEventManager", 0xb4e7e0, 32, 0x310aec70, reinterpret_cast<void *>(&SmoothFirstPersonCopy::HookGetEventManager)},
    {"ConfigDeviceSettingsBehavior_Release", 0xb6f9f0, 32, 0x8108ae5d, MemberAddress(&ConfigDeviceSettingsCopy::HookRelease)},
    {"ConfigDeviceSettingsBehavior_OnEvent", 0xb6ede0, 32, 0xca83d5e9, MemberAddress(&ConfigDeviceSettingsCopy::HookOnEvent)},
    {"ConfigDeviceSettingsBehavior_NotifyListeners", 0xb6fa40, 32, 0x098a7d26, MemberAddress(&ConfigDeviceSettingsCopy::HookNotifyListeners)},
    {"ConfigDeviceSettingsBehavior_AskListeners", 0xb6ec20, 32, 0x6a2520c5, MemberAddress(&ConfigDeviceSettingsCopy::HookAskListeners)},
    {"ConfigDeviceSettingsBehavior_Connect", 0xb6eb60, 32, 0x5f8b04f0, MemberAddress(&ConfigDeviceSettingsCopy::HookConnect)},
    {"ConfigDeviceSettingsBehavior_Disconnect", 0xb6ec00, 32, 0x4d13b28e, MemberAddress(&ConfigDeviceSettingsCopy::HookDisconnect)},
    {"ConfigDeviceSettingsBehavior_GetEventCount", 0xb6f0f0, 32, 0x8ff279cf, MemberAddress(&ConfigDeviceSettingsCopy::HookGetEventCount)},
    {"ConfigDeviceSettingsBehavior_GetEventAttribute", 0xb6ea80, 32, 0x44150bbb, MemberAddress(&ConfigDeviceSettingsCopy::HookGetEventAttribute)},
    {"ConfigDeviceSettingsBehavior_GetEventManager", 0xb6eb80, 32, 0x0bd5bb4d, reinterpret_cast<void *>(&ConfigDeviceSettingsCopy::HookGetEventManager)},
    {"MainToolbarBehavior_Release", 0x620469, 32, 0x7a89e1ba, MemberAddress(&MainToolbarCopy::HookRelease)},
    {"MainToolbarBehavior_OnEvent", 0x61e397, 32, 0xd59b628e, MemberAddress(&MainToolbarCopy::HookOnEvent)},
    {"MainToolbarBehavior_NotifyListeners", 0x620a60, 32, 0x9a5ccc97, MemberAddress(&MainToolbarCopy::HookNotifyListeners)},
    {"MainToolbarBehavior_AskListeners", 0x61e01f, 32, 0x095b2c96, MemberAddress(&MainToolbarCopy::HookAskListeners)},
    {"MainToolbarBehavior_Connect", 0x61b5b4, 32, 0x5f8b04f0, MemberAddress(&MainToolbarCopy::HookConnect)},
    {"MainToolbarBehavior_Disconnect", 0x61b97c, 32, 0xc597d405, MemberAddress(&MainToolbarCopy::HookDisconnect)},
    {"MainToolbarBehavior_GetEventCount", 0x6200b5, 9, 0x723dd359, MemberAddress(&MainToolbarCopy::HookGetEventCount)},
    {"MainToolbarBehavior_GetEventAttribute", 0x61b062, 32, 0x6ffe098a, MemberAddress(&MainToolbarCopy::HookGetEventAttribute)},
    {"MainToolbarBehavior_GetEventManager", 0x61b5d4, 32, 0x548de62d, reinterpret_cast<void *>(&MainToolbarCopy::HookGetEventManager)},
    {"ScreenGrabberBehavior_Release", 0x60e6c2, 32, 0x3f847b6c, MemberAddress(&ScreenGrabberCopy::HookRelease)},
    {"ScreenGrabberBehavior_OnEvent", 0x60e426, 32, 0xf5b487e0, MemberAddress(&ScreenGrabberCopy::HookOnEvent)},
    {"ScreenGrabberBehavior_NotifyListeners", 0x60e71f, 32, 0xaafcadf2, MemberAddress(&ScreenGrabberCopy::HookNotifyListeners)},
    {"ScreenGrabberBehavior_AskListeners", 0x60dd3e, 32, 0x29b3b6ed, MemberAddress(&ScreenGrabberCopy::HookAskListeners)},
    {"ScreenGrabberBehavior_Connect", 0x60dcb4, 32, 0x5f8b04f0, MemberAddress(&ScreenGrabberCopy::HookConnect)},
    {"ScreenGrabberBehavior_Disconnect", 0x60dd1e, 32, 0xe471a4d0, MemberAddress(&ScreenGrabberCopy::HookDisconnect)},
    {"ScreenGrabberBehavior_GetEventCount", 0x60e47f, 9, 0x5434ae42, MemberAddress(&ScreenGrabberCopy::HookGetEventCount)},
    {"ScreenGrabberBehavior_GetEventAttribute", 0x60dc7c, 32, 0xee32da52, MemberAddress(&ScreenGrabberCopy::HookGetEventAttribute)},
    {"ScreenGrabberBehavior_GetEventManager", 0x60dcd4, 32, 0x7dd9b453, reinterpret_cast<void *>(&ScreenGrabberCopy::HookGetEventManager)},
    {"WinSceneGraphBehavior_Release", 0x5bbe48, 32, 0x8b3e2bba, MemberAddress(&WinSceneGraphCopy::HookRelease)},
    {"WinSceneGraphBehavior_OnEvent", 0x5ba6c0, 32, 0x5ae91239, MemberAddress(&WinSceneGraphCopy::HookOnEvent)},
    {"WinSceneGraphBehavior_NotifyListeners", 0x5bc64e, 32, 0xbab0947e, MemberAddress(&WinSceneGraphCopy::HookNotifyListeners)},
    {"WinSceneGraphBehavior_AskListeners", 0x5b989b, 32, 0xf6051330, MemberAddress(&WinSceneGraphCopy::HookAskListeners)},
    {"WinSceneGraphBehavior_Connect", 0x5b8af6, 32, 0x5f8b04f0, MemberAddress(&WinSceneGraphCopy::HookConnect)},
    {"WinSceneGraphBehavior_Disconnect", 0x5b8c7b, 32, 0xadd02c60, MemberAddress(&WinSceneGraphCopy::HookDisconnect)},
    {"WinSceneGraphBehavior_GetEventAttribute", 0x5b861a, 32, 0xa715419e, MemberAddress(&WinSceneGraphCopy::HookGetEventAttribute)},
    {"WinSceneGraphBehavior_GetEventManager", 0x5b8b16, 32, 0xcc6dba96, reinterpret_cast<void *>(&WinSceneGraphCopy::HookGetEventManager)},
};
REGISTER_HOOKS(kHooks);

} // namespace
