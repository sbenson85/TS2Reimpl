// cTSUIServiceManager (lane KM, src/ui/cTSUIServiceManager.h): the factory the COM director registers (class 0xebd09b02).
// Every manager is ours; the constructor, destructor, slots and the two helpers are in effect through it.
#include "hook/hooks.h"
#include "ui/cTSUIServiceManager.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSUIServiceManager_Create", 0x73f8af, 32, 0xa2497c04, reinterpret_cast<void *>(&cTSUIServiceManager::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
