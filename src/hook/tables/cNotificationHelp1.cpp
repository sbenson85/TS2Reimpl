// Hooks for nRZSTD::cNotificationHelp1 (src/rz/cNotificationHelp1.cpp), lane MC. 25 classes build one inline
// (the game's vtable 0xde21d8), so the two slot bodies are hooked at their RVAs.
#include "hook/hooks.h"
#include "rz/cNotificationHelp1.h"

namespace {

const HookSpec kHooks[] = {
    {"cNotificationHelp1_AddNotifications", 0xb6d960, 32, 0xb8af4d56, MemberAddress(&nRZSTD::cNotificationHelp1::AddNotifications)},
    {"cNotificationHelp1_RemoveNotifications", 0xb6e070, 32, 0xb8af4d56, MemberAddress(&nRZSTD::cNotificationHelp1::RemoveNotifications)},
};
REGISTER_HOOKS(kHooks);

} // namespace
