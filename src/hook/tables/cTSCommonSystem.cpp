#include "hook/hooks.h"
#include "ts/cTSCommonSystem.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSCommonSystem_Create", 0xcb9dd0, 32, 0x6954f711, reinterpret_cast<void *>(&cTSCommonSystem::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
