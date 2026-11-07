#include "hook/hooks.h"
#include "ts/cTSConfigurationManager.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSConfigurationManager_Create", 0xcd0360, 32, 0x58ed1afb, reinterpret_cast<void *>(&cTSConfigurationManager::Create)},
    {"cTSConfigurationManager_SetCurrentDevice", 0x7c4ff0, 13, 0x63d6ebcf, reinterpret_cast<void *>(&cTSConfigurationManagerAdapters::SetCurrentDevice)},
};
REGISTER_HOOKS(kHooks);

} // namespace
